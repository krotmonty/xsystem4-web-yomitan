/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "audio.h"
#include "input.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "json.h"
#include "cJSON.h"
#include "plugin.h"
#include "sprite.h"
#include "vm.h"
#include "vm/page.h"
#include "xsystem4.h"

bool sact_dirty = true;

LIST_HEAD(listhead, sact_sprite) sprites_with_plugins =
	LIST_HEAD_INITIALIZER(sprites_with_plugins);

// XXX: sprites should typically be set to zero and then initialized with this
//      function upon allocation.
void sprite_init(struct sact_sprite *sp)
{
	sp->blend_rate = 255;
	sp->multiply_color = (SDL_Color){255,255,255,255};
}

void sprite_free(struct sact_sprite *sp)
{
	scene_unregister_sprite(&sp->sp);
	gfx_delete_texture(&sp->texture);
	gfx_delete_texture(&sp->text.texture);
	free(sp->text.glyphs);
	if (sp->plugin) {
		if (sp->plugin->free)
			sp->plugin->free(sp->plugin);
		LIST_REMOVE(sp, entry);
	}
	// restore to initial state
	memset(sp, 0, sizeof(struct sact_sprite));
	sprite_init(sp);
}

static void sprite_init_texture(struct sact_sprite *sp)
{
	if (!sp->texture.handle) {
		if (sp->sp.has_alpha)
			gfx_init_texture_rgba(&sp->texture, sp->rect.w, sp->rect.h, sp->color);
		else
			gfx_init_texture_rgb(&sp->texture, sp->rect.w, sp->rect.h, sp->color);
	}
}

static struct sprite_shader {
	struct shader s;
	GLuint blend_rate;
	GLuint multiply_color;
	GLuint add_color;
	GLuint bot_left;
	GLuint top_right;
} sprite_shader = {0};

static void prepare_sact_shader(struct gfx_render_job *job, void *data)
{
	struct sprite_shader *s = (struct sprite_shader*)job->shader;
	struct sact_sprite *sp = (struct sact_sprite*)data;
	glUniform1f(s->blend_rate, sp->blend_rate / 255.0f);
	glUniform3f(s->multiply_color,
			sp->multiply_color.r / 255.0f,
			sp->multiply_color.g / 255.0f,
			sp->multiply_color.b / 255.0f);
}

static void prepare_chipmunk_shader(struct gfx_render_job *job, void *data)
{
	struct sprite_shader *s = (struct sprite_shader*)job->shader;
	struct sact_sprite *sp = (struct sact_sprite*)data;
	glUniform1f(s->blend_rate, sp->blend_rate / 255.0f);
	glUniform3f(s->multiply_color,
			sp->multiply_color.r / 255.0f,
			sp->multiply_color.g / 255.0f,
			sp->multiply_color.b / 255.0f);
	glUniform3f(s->add_color,
			sp->add_color.r / 255.0f,
			sp->add_color.g / 255.0f,
			sp->add_color.b / 255.0f);

	Rectangle r = sp->surface_area;
	if (!r.w && !r.h) {
		r = (Rectangle) { 0, 0, sp->rect.w, sp->rect.h };
	}

	glUniform2f(s->bot_left, r.x, r.y);
	glUniform2f(s->top_right, r.x + r.w, r.y + r.h);
}

void sprite_init_sact(void)
{
	if (sprite_shader.s.prepare) {
		if (sprite_shader.s.prepare != prepare_sact_shader) {
			WARNING("mixed SACT2/ChipmunkSpriteEngine initialization");
			return;
		}
	}
	gfx_load_shader(&sprite_shader.s, "shaders/render.v.glsl", "shaders/sprite.f.glsl");
	sprite_shader.blend_rate = glGetUniformLocation(sprite_shader.s.program, "blend_rate");
	sprite_shader.multiply_color = glGetUniformLocation(sprite_shader.s.program, "multiply_color");
	sprite_shader.s.prepare = prepare_sact_shader;

}

void sprite_init_chipmunk(void)
{
	if (sprite_shader.s.prepare) {
		if (sprite_shader.s.prepare != prepare_chipmunk_shader) {
			WARNING("mixed SACT2/ChipmunkSpriteEngine initialization");
		} else {
			return;
		}
	}
	gfx_load_shader(&sprite_shader.s, "shaders/parts.v.glsl", "shaders/parts.f.glsl");
	sprite_shader.blend_rate = glGetUniformLocation(sprite_shader.s.program, "blend_rate");
	sprite_shader.multiply_color = glGetUniformLocation(sprite_shader.s.program, "multiply_color");
	sprite_shader.add_color = glGetUniformLocation(sprite_shader.s.program, "add_color");
	sprite_shader.bot_left = glGetUniformLocation(sprite_shader.s.program, "bot_left");
	sprite_shader.top_right = glGetUniformLocation(sprite_shader.s.program, "top_right");
	sprite_shader.s.prepare = prepare_chipmunk_shader;
	glUseProgram(sprite_shader.s.program);
	glUniform1i(glGetUniformLocation(sprite_shader.s.program, "use_clipper"), 0);
	glUseProgram(0);
}

static void sprite_render(struct sprite *_sp)
{
	struct sact_sprite *sp = (struct sact_sprite*)_sp;
	if (sp->plugin && sp->plugin->render) {
		sp->plugin->render(sp);
		return;
	}

	sprite_init_texture(sp);

	switch (sp->draw_method) {
	case DRAW_METHOD_SCREEN:
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
		break;
	case DRAW_METHOD_MULTIPLY:
		glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
		break;
	case DRAW_METHOD_ADDITIVE:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	case DRAW_METHOD_NORMAL:
		break;
	}

	Rectangle r = sp->rect;
	if (sp->surface_area.x || sp->surface_area.y) {
		r.x -= sp->surface_area.x;
		r.y -= sp->surface_area.y;
	}

	_gfx_render_texture(&sprite_shader.s, &sp->texture, &r, sp);
	if (sp->text.texture.handle) {
		_gfx_render_texture(&sprite_shader.s, &sp->text.texture, &r, sp);
	}

	if (sp->draw_method != DRAW_METHOD_NORMAL)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

struct texture *sprite_get_texture(struct sact_sprite *sp)
{
	sprite_init_texture(sp);
	return &sp->texture;
}

static cJSON *_sprite_to_json(struct sprite *sp, bool verbose)
{
	return sprite_to_json((struct sact_sprite*)sp, verbose);
}

void sprite_set_cg(struct sact_sprite *sp, struct cg *cg)
{
	gfx_delete_texture(&sp->texture);
	gfx_init_texture_with_cg(&sp->texture, cg);
	sp->rect.w = cg->metrics.w;
	sp->rect.h = cg->metrics.h;
	sp->sp.has_pixel = true;
	sp->sp.has_alpha = cg->metrics.has_alpha;
	sp->sp.render = sprite_render;
	sp->sp.to_json = _sprite_to_json;
	sprite_dirty(sp);
}

// XXX: Only used in English Rance VI
void sprite_set_cg_2x(struct sact_sprite *sp, struct cg *cg)
{
	// load CG at 1x
	Texture tmp;
	gfx_init_texture_with_cg(&tmp, cg);

	SDL_Color c = { 0, 0, 0, 255 };
	int w = cg->metrics.w*2;
	int h = cg->metrics.h*2;

	// upscale into sprite texture
	gfx_delete_texture(&sp->texture);
	if (cg->metrics.has_alpha) {
		gfx_init_texture_rgba(&sp->texture, w, h, c);
	} else {
		gfx_init_texture_rgb(&sp->texture, w, h, c);
	}
	gfx_copy_stretch_with_alpha_map(&sp->texture, 0, 0, w, h, &tmp, 0, 0, cg->metrics.w, cg->metrics.h);

	sp->rect.w = w;
	sp->rect.h = h;
	sp->sp.has_pixel = true;
	sp->sp.has_alpha = cg->metrics.has_alpha;
	sp->sp.render = sprite_render;
	sp->sp.to_json = _sprite_to_json;
	sprite_dirty(sp);
	gfx_delete_texture(&tmp);
}

int sprite_set_cg_from_asset(struct sact_sprite *sp, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;
	sprite_set_cg(sp, cg);
	sp->cg_no = cg_no;
	cg_free(cg);
	return 1;
}

int sprite_set_cg_2x_from_asset(struct sact_sprite *sp, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;
	sprite_set_cg_2x(sp, cg);
	sp->cg_no = cg_no;
	cg_free(cg);
	return 1;
}

int sprite_set_cg_by_name(struct sact_sprite *sp, const char *name)
{
	struct cg *cg = asset_cg_load_by_name(name, &sp->cg_no);
	if (!cg)
		return 0;
	sprite_set_cg(sp, cg);
	cg_free(cg);
	return 1;
}

int sprite_set_cg_from_file(struct sact_sprite *sp, const char *path)
{
	struct cg *cg = cg_load_file(path);
	if (!cg)
		return 0;
	sprite_set_cg(sp, cg);
	cg_free(cg);
	return 1;
}

int sprite_save_cg(struct sact_sprite *sp, const char *path)
{
	return gfx_save_texture(sprite_get_texture(sp), path, ALCG_QNT);
}

/*
 * Attach pixel data to a sprite. The texture is initialized lazily.
 */
void sprite_init_color(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a)
{
	sp->color = (SDL_Color) { .r = r, .g = g, .b = b, .a = (a >= 0 ? a : 255) };
	sp->rect.w = w;
	sp->rect.h = h;
	gfx_delete_texture(&sp->texture);

	sp->sp.has_pixel = true;
	sp->sp.has_alpha = a >= 0;
	sp->sp.render = sprite_render;
	sp->sp.to_json = _sprite_to_json;
	sprite_dirty(sp);
}

void sprite_init_custom(struct sact_sprite *sp)
{
	sp->sp.render = sprite_render;
	sp->sp.to_json = _sprite_to_json;
}

void sprite_set_pos(struct sact_sprite *sp, int x, int y)
{
	sp->rect.x = x;
	sp->rect.y = y;
	sprite_dirty(sp);
}

void sprite_set_x(struct sact_sprite *sp, int x)
{
	sp->rect.x = x;
	sprite_dirty(sp);
}

void sprite_set_y(struct sact_sprite *sp, int y)
{
	sp->rect.y = y;
	sprite_dirty(sp);
}

void sprite_set_blend_rate(struct sact_sprite *sp, int rate)
{
	sp->blend_rate = max(0, min(255, rate));
	sprite_dirty(sp);
}

void sprite_set_multiply_color(struct sact_sprite *sp, int r, int g, int b)
{
	sp->multiply_color.r = max(0, min(255, r));
	sp->multiply_color.g = max(0, min(255, g));
	sp->multiply_color.b = max(0, min(255, b));
}

void sprite_set_add_color(struct sact_sprite *sp, int r, int g, int b)
{
	sp->add_color.r = max(0, min(255, r));
	sp->add_color.g = max(0, min(255, g));
	sp->add_color.b = max(0, min(255, b));
}

void sprite_set_surface_area(struct sact_sprite *sp, int x, int y, int w, int h)
{
	sp->surface_area = (Rectangle) { x, y, w, h };
}

int sprite_set_draw_method(struct sact_sprite *sp, enum draw_method method)
{
	sp->draw_method = method;
	sprite_dirty(sp);
	return 1;
}

enum draw_method sprite_get_draw_method(struct sact_sprite *sp)
{
	return sp->draw_method;
}

void sprite_set_text_home(struct sact_sprite *sp, int x, int y)
{
	sp->text.home = (Point) { .x = x, .y = y };
	sprite_dirty(sp);
}

void sprite_set_text_line_space(struct sact_sprite *sp, int px)
{
	sp->text.line_space = px;
	sprite_dirty(sp);
}

void sprite_set_text_char_space(struct sact_sprite *sp, int px)
{
	sp->text.char_space = px;
	sprite_dirty(sp);
}

void sprite_set_text_pos(struct sact_sprite *sp, int x, int y)
{
	if (sp->text.pos.y != y)
		sp->text.current_line_height = 0;
	sp->text.pos = (Point) { .x = x, .y = y };
	sprite_dirty(sp);
}

struct glyph_record_ctx {
	struct sact_sprite *sp;
	float origin_x;
	int y;
	int h;
};

static void sprite_record_glyph(void *data, const char *ch, int len, float x, float w)
{
	struct glyph_record_ctx *ctx = data;
	struct sact_sprite *sp = ctx->sp;
	if (len < 1 || len > 3)
		return;
	float gx = ctx->origin_x + x;

	// A character drawn over an existing one replaces it.
	for (int i = 0; i < sp->text.nr_glyphs; i++) {
		struct sprite_glyph *g = &sp->text.glyphs[i];
		if (g->y == ctx->y && fabsf(g->x - gx) < 0.5f) {
			memmove(g, g + 1, (sp->text.nr_glyphs - i - 1) * sizeof(*g));
			sp->text.nr_glyphs--;
			break;
		}
	}

	if (sp->text.nr_glyphs >= sp->text.cap_glyphs) {
		int cap = sp->text.cap_glyphs ? sp->text.cap_glyphs * 2 : 64;
		struct sprite_glyph *p = realloc(sp->text.glyphs, cap * sizeof(*p));
		if (!p)
			return;
		sp->text.glyphs = p;
		sp->text.cap_glyphs = cap;
	}
	struct sprite_glyph *g = &sp->text.glyphs[sp->text.nr_glyphs++];
	memset(g->ch, 0, sizeof(g->ch));
	memcpy(g->ch, ch, len);
	g->x = gx;
	g->w = w;
	g->y = ctx->y;
	g->h = ctx->h;
}

static void sprite_clear_glyphs(struct sact_sprite *sp)
{
	free(sp->text.glyphs);
	sp->text.glyphs = NULL;
	sp->text.nr_glyphs = 0;
	sp->text.cap_glyphs = 0;
}

void sprite_text_draw(struct sact_sprite *sp, struct string *text, struct text_style *ts)
{
	if (!sp->text.texture.handle) {
		gfx_init_texture_rgba(&sp->text.texture, sp->rect.w, sp->rect.h, COLOR(0, 0, 0, 0));
	}

	ts->font_spacing = sp->text.char_space;
	struct glyph_record_ctx ctx = {
		.sp = sp,
		.origin_x = sp->text.pos.x,
		.y = sp->text.pos.y,
		.h = (int)ts->size,
	};
	gfx_layout_text(ts, text->text, sprite_record_glyph, &ctx);
	sp->text.pos.x += gfx_render_text(&sp->text.texture, sp->text.pos.x, sp->text.pos.y, text->text, ts, false);
	if (sp->text.current_line_height < ts->size)
		sp->text.current_line_height = ts->size;
	sprite_dirty(sp);
}

void sprite_text_clear(struct sact_sprite *sp)
{
	gfx_delete_texture(&sp->text.texture);
	sprite_clear_glyphs(sp);
	sprite_dirty(sp);
}

void sprite_text_home(struct sact_sprite *sp, int size)
{
	sp->text.pos = sp->text.home;
	sp->text.current_line_height = size;
	sprite_dirty(sp);
}

void sprite_text_new_line(struct sact_sprite *sp, int size)
{
	if (!size)
		size = sp->text.current_line_height;
	sp->text.pos = POINT(sp->text.home.x, sp->text.pos.y + size + sp->text.line_space);
	sp->text.current_line_height = 0;
	sprite_dirty(sp);
}

void sprite_text_copy(struct sact_sprite *dsp, struct sact_sprite *ssp)
{
	sprite_text_clear(dsp);
	if (ssp->text.texture.handle) {
		SDL_Color c = { 0, 0, 0, 0 };
		gfx_init_texture_rgba(&dsp->text.texture, dsp->rect.w, dsp->rect.h, c);
		gfx_copy(&dsp->text.texture, 0, 0, &ssp->text.texture, 0, 0, ssp->text.texture.w, ssp->text.texture.h);
		gfx_copy_amap(&dsp->text.texture, 0, 0, &ssp->text.texture, 0, 0, ssp->text.texture.w, ssp->text.texture.h);
	}
	if (ssp->text.nr_glyphs) {
		size_t size = ssp->text.nr_glyphs * sizeof(struct sprite_glyph);
		dsp->text.glyphs = malloc(size);
		if (dsp->text.glyphs) {
			memcpy(dsp->text.glyphs, ssp->text.glyphs, size);
			dsp->text.nr_glyphs = ssp->text.nr_glyphs;
			dsp->text.cap_glyphs = ssp->text.nr_glyphs;
		}
	}
	dsp->text.home = ssp->text.home;
	dsp->text.pos = ssp->text.pos;
	dsp->text.char_space = ssp->text.char_space;
	dsp->text.line_space = ssp->text.line_space;
	dsp->text.current_line_height = ssp->text.current_line_height;
	sprite_dirty(dsp);
}

bool sprite_is_point_in_rect(struct sact_sprite *sp, int x, int y)
{
	Point p = POINT(x, y);
	return !!SDL_PointInRect(&p, &sp->rect);
}

bool sprite_is_point_in(struct sact_sprite *sp, int x, int y)
{
	if (!sprite_is_point_in_rect(sp, x, y))
		return 0;

	// check alpha
	struct texture *t = sprite_get_texture(sp);
	SDL_Color c = gfx_get_pixel(t, x - sp->rect.x, y - sp->rect.y);
	return !!c.a;
}

int sprite_get_amap_value(struct sact_sprite *sp, int x, int y)
{
	return gfx_get_pixel(sprite_get_texture(sp), x, y).a;
}

void sprite_get_pixel_value(struct sact_sprite *sp, int x, int y, int *r, int *g, int *b)
{
	SDL_Color c = gfx_get_pixel(sprite_get_texture(sp), x, y);
	*r = c.r;
	*g = c.g;
	*b = c.b;
}

void sprite_bind_plugin(struct sact_sprite *sp, struct draw_plugin *plugin)
{
	if (!sp->plugin && plugin)
		LIST_INSERT_HEAD(&sprites_with_plugins, sp, entry);
	else if (sp->plugin && !plugin)
		LIST_REMOVE(sp, entry);
	if (sp->plugin != plugin && sp->plugin && sp->plugin->free)
		sp->plugin->free(sp->plugin);
	sp->plugin = plugin;
}

void sprite_call_plugins(void)
{
	struct sact_sprite *sp;
	LIST_FOREACH(sp, &sprites_with_plugins, entry) {
		if (sp->plugin->update)
			sp->plugin->update(sp);
	}
}


/*
 * In-game Yomitan text layer.
 *
 * Every time the scene is rendered, the text drawn on visible sprites is
 * collected (with its position on screen) and, if it changed, sent to the web
 * page, which places invisible HTML text over the canvas so that browser
 * pop-up dictionaries such as Yomitan can scan it.
 */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static cJSON *textmap_lines;
static char *textmap_last;

void textmap_begin(void)
{
	cJSON_Delete(textmap_lines);
	textmap_lines = cJSON_CreateArray();
}

static void textmap_add_line(Rectangle *r, struct sprite_glyph *first, int n)
{
	char sjis[1024];
	size_t len = 0;
	for (int i = 0; i < n; i++) {
		size_t l = strlen(first[i].ch);
		if (len + l >= sizeof(sjis))
			break;
		memcpy(sjis + len, first[i].ch, l);
		len += l;
	}
	sjis[len] = '\0';
	if (!len)
		return;

	struct sprite_glyph *last = &first[n - 1];
	float x = r->x + first->x;
	float w = last->x + last->w - first->x;
	int y = r->y + first->y;

	char *utf = sjis2utf(sjis, len);
	cJSON *line = cJSON_CreateArray();
	cJSON_AddItemToArray(line, cJSON_CreateNumber(roundf(x * 10) / 10));
	cJSON_AddItemToArray(line, cJSON_CreateNumber(y));
	cJSON_AddItemToArray(line, cJSON_CreateNumber(roundf(w * 10) / 10));
	cJSON_AddItemToArray(line, cJSON_CreateNumber(first->h));
	cJSON_AddItemToArray(line, cJSON_CreateString(utf));
	cJSON_AddItemToArray(textmap_lines, line);
	free(utf);
}

void textmap_collect(struct sprite *_sp)
{
	if (!textmap_lines || _sp->render != sprite_render)
		return;
	struct sact_sprite *sp = (struct sact_sprite*)_sp;
	if (!sp->text.nr_glyphs || !sp->text.texture.handle || sp->blend_rate <= 0)
		return;

	Rectangle r = sp->rect;
	if (sp->surface_area.x || sp->surface_area.y) {
		r.x -= sp->surface_area.x;
		r.y -= sp->surface_area.y;
	}

	// Group consecutive characters on the same row into lines.
	struct sprite_glyph *g = sp->text.glyphs;
	int start = 0;
	for (int i = 1; i <= sp->text.nr_glyphs; i++) {
		bool split = i == sp->text.nr_glyphs
			|| g[i].y != g[i-1].y
			|| fabsf(g[i].x - (g[i-1].x + g[i-1].w)) > 2.0f;
		if (!split)
			continue;
		// skip text outside the sprite's texture
		if (g[start].y < sp->text.texture.h && g[start].x < sp->text.texture.w)
			textmap_add_line(&r, &g[start], i - start);
		start = i;
	}
}

void textmap_end(void)
{
	if (!textmap_lines)
		return;
	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "w", config.view_width);
	cJSON_AddNumberToObject(root, "h", config.view_height);
	cJSON_AddItemToObject(root, "l", textmap_lines);
	textmap_lines = NULL;
	char *json = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!json)
		return;

	if (textmap_last && !strcmp(json, textmap_last)) {
		free(json);
		return;
	}
	free(textmap_last);
	textmap_last = json;

	// The page frees the copy once it has read it.
	char *copy = strdup(json);
	if (!copy)
		return;
	MAIN_THREAD_ASYNC_EM_ASM({
		const json = UTF8ToString($0);
		Module._free($0);
		const shell = Module.shell;
		if (shell && shell.yomi)
			shell.yomi.update(json);
	}, copy);
}

#else

void textmap_begin(void) {}
void textmap_collect(struct sprite *sp) {}
void textmap_end(void) {}

#endif
