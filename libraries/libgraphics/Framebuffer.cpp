/*
	This file is part of duckOS.

	duckOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	duckOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with duckOS.  If not, see <https://www.gnu.org/licenses/>.

	Copyright (c) Byteduck 2016-2021. All rights reserved.
*/

#include "Framebuffer.h"
#include "Graphics.h"
#include "Font.h"
#include "Memory.h"
#include "Geometry.h"

using namespace Gfx;

Framebuffer::Framebuffer(): data(nullptr), width(0), height(0) {}
Framebuffer::Framebuffer(Color* buffer, int width, int height): data(buffer), width(width), height(height) {}
Framebuffer::Framebuffer(int width, int height): data(new Color[width * height]), width(width), height(height), should_free(true) {}
Framebuffer::Framebuffer(Framebuffer&& other) noexcept: data(other.data), width(other.width), height(other.height), should_free(other.should_free) {
	other.data = nullptr;
}
Framebuffer::Framebuffer(Framebuffer& other) noexcept: data(other.data), width(other.width), height(other.height), should_free(false) {}

Framebuffer::~Framebuffer() noexcept {
	if(should_free)
		free();
}

Framebuffer& Framebuffer::operator=(const Framebuffer& other) {
	if(should_free)
		free();
	width = other.width;
	height = other.height;
	data = other.data;
	should_free = false;
	return *this;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
	if(should_free)
		free();
	width = other.width;
	height = other.height;
	data = other.data;
	should_free = other.should_free;
	other.data = nullptr;
	return *this;
}

void Framebuffer::free() {
	if(data) {
		delete data;
		data = nullptr;
	}
}

void Framebuffer::copy(const Framebuffer& other, Rect other_area, const Point& pos) const {
	//Make sure self_area is in bounds of the framebuffer
	Rect self_area = {pos.x, pos.y, other_area.width, other_area.height};
	self_area = self_area.overlapping_area({0, 0, width, height});
	if(self_area.empty())
		return;

	//Update other area with the changes made to self_area
	other_area.x += self_area.x - pos.x;
	other_area.y += self_area.y - pos.y;
	other_area.width = self_area.width;
	other_area.height = self_area.height;

	for(int y = 0; y < self_area.height; y++) {
		memcpy_uint32((uint32_t*) &data[self_area.x + (self_area.y + y) * width], (uint32_t*) &other.data[other_area.x + (other_area.y + y) * other.width], self_area.width);
	}
}

void Framebuffer::copy_noalpha(const Framebuffer& other, Rect other_area, const Point& pos) const {
	//Make sure self_area is in bounds of the framebuffer
	Rect self_area = {pos.x, pos.y, other_area.width, other_area.height};
	self_area = self_area.overlapping_area({0, 0, width, height});
	if(self_area.empty())
		return;

	//Update other area with the changes made to self_area
	other_area.x += self_area.x - pos.x;
	other_area.y += self_area.y - pos.y;
	other_area.width = self_area.width;
	other_area.height = self_area.height;

	for(int y = 0; y < self_area.height; y++) {
		for(int x = 0; x < self_area.width; x++) {
			data[(self_area.x + x) + (self_area.y + y) * width] = RGBA(0, 0, 0, 255) | other.data[(other_area.x + x) + (other_area.y + y) * other.width];
		}
	}
}

void Framebuffer::copy_blitting(const Framebuffer& other, Rect other_area, const Point& pos) const {
	//Make sure self_area is in bounds of the framebuffer
	Rect self_area = {pos.x, pos.y, other_area.width, other_area.height};
	self_area = self_area.overlapping_area({0, 0, width, height});
	if(self_area.empty())
		return;

	//Update other area with the changes made to self_area
	other_area.x += self_area.x - pos.x;
	other_area.y += self_area.y - pos.y;
	other_area.width = self_area.width;
	other_area.height = self_area.height;

	for(int y = 0; y < self_area.height; y++) {
		for(int x = 0; x < self_area.width; x++) {
			auto& this_val = data[(self_area.x + x) + (self_area.y + y) * width];
			auto& other_val = other.data[(other_area.x + x) + (other_area.y + y) * other.width];
			this_val = this_val.blended(other_val);
		}
	}
}

void Framebuffer::copy_blitting_flipped(const Framebuffer& other, Rect other_area, const Point& pos, bool flip_h, bool flip_v) const {
	//Make sure self_area is in bounds of the framebuffer
	Rect self_area = {pos.x, pos.y, other_area.width, other_area.height};
	self_area = self_area.overlapping_area({0, 0, width, height});
	if(self_area.empty())
		return;

	//Update other area with the changes made to self_area
	other_area.x += self_area.x - pos.x;
	other_area.y += self_area.y - pos.y;
	other_area.width = self_area.width;
	other_area.height = self_area.height;

	for(int y = 0; y < self_area.height; y++) {
		for(int x = 0; x < self_area.width; x++) {
			auto& this_val = data[(self_area.x + x) + (self_area.y + y) * width];
			auto& other_val = other.data[
				 other_area.x + (flip_h ? other_area.width - x - 1 : x) +
				(other_area.y + (flip_v ? other_area.height - y - 1 : y)) * other.width
			];
			this_val = this_val.blended(other_val);
		}
	}
}

void Framebuffer::copy_tiled(const Framebuffer& other, Rect other_area, const Point& pos) const {
	//Make sure self_area is in bounds of the framebuffer
	Rect self_area = {pos.x, pos.y, other_area.width, other_area.height};
	self_area = self_area.overlapping_area({0, 0, width, height});
	if(self_area.empty())
		return;

	//Update other area with the changes made to self_area
	other_area.x += self_area.x - pos.x;
	other_area.y += self_area.y - pos.y;
	other_area.width = self_area.width;
	other_area.height = self_area.height;

	for(int y = 0; y < self_area.height; y++) {
		for(int x = 0; x < self_area.width; x++) {
			data[(self_area.x + x) + (self_area.y + y) * width] = other.data[(((other_area.x + x) % other.width) + ((other_area.y + y) % other.height) * other.width) % (other.width * other.height)];
		}
	}
}

void Framebuffer::draw_image(const Framebuffer& other, Rect other_area, const Point& pos) const {
	//Make sure self_area is in bounds of the framebuffer
	Rect self_area = {pos.x, pos.y, other_area.width, other_area.height};
	self_area = self_area.overlapping_area({0, 0, width, height});
	if(self_area.empty())
		return;

	//Update other area with the changes made to self_area
	other_area.x += self_area.x - pos.x;
	other_area.y += self_area.y - pos.y;
	other_area.width = self_area.width;
	other_area.height = self_area.height;

	for(int y = 0; y < self_area.height; y++) {
		for(int x = 0; x < self_area.width; x++) {
			auto& this_val = data[(self_area.x + x) + (self_area.y + y) * width];
			auto& other_val = other.data[(other_area.x + x) + (other_area.y + y) * other.width];
			this_val = this_val.blended(other_val);
		}
	}
}

void Framebuffer::draw_image(const Framebuffer& other, const Point& pos) const {
	draw_image(other, {0, 0, other.width, other.height}, pos);
}

void Framebuffer::draw_image_scaled(const Framebuffer& other, const Rect& rect) const {
	if(rect.width == other.width && rect.height == other.height) {
		draw_image(other, rect.position());
		return;
	}

	double scale_x = (double) rect.width / (double) other.width;
	double scale_y = (double) rect.height / (double) other.height;

	//Make sure self_area is in bounds of the framebuffer
	Rect self_area = rect;
	self_area = self_area.overlapping_area({0, 0, width, height});
	if(self_area.empty())
		return;

	//Update other area with the changes made to self_area
	DoubleRect other_area = {0, 0, (float) other.width, (float) other.height};
	other_area.x += (self_area.x - rect.x) / scale_x;
	other_area.y += (self_area.y - rect.y) / scale_y;
	other_area.width = self_area.width / scale_x;
	other_area.height = self_area.height / scale_y;

	for(int y = 0; y < self_area.height; y++) {
		for(int x = 0; x < self_area.width; x++) {
			auto& this_val = data[(self_area.x + x) + (self_area.y + y) * width];
			auto& other_val = other.data[(int) (other_area.x + x / scale_x) + (int) (other_area.y + y / scale_y) * other.width];
			this_val = this_val.blended(other_val);
		}
	}
}

void Framebuffer::fill(Rect area, Color color) const {
	//Make sure area is in the bounds of the framebuffer
	area = area.overlapping_area({0, 0, width, height});
	if(area.empty())
		return;

	for(int y = 0; y < area.height; y++) {
		for(int x = 0; x < area.width; x++) {
			data[x + area.x + (area.y + y) * width] = color;
		}
	}
}

void Framebuffer::fill_blitting(Rect area, Color color) const {
	//Make sure area is in the bounds of the framebuffer
	area = area.overlapping_area({0, 0, width, height});
	if(area.empty())
		return;

	unsigned int alpha = COLOR_A(color) + 1;
	unsigned int inv_alpha = 256 - COLOR_A(color);
	unsigned int premultiplied_r = alpha * COLOR_R(color);
	unsigned int premultiplied_g = alpha * COLOR_G(color);
	unsigned int premultiplied_b = alpha * COLOR_B(color);
	unsigned int premultiplied_a = alpha * COLOR_A(color);

	for(int y = 0; y < area.height; y++) {
		for(int x = 0; x < area.width; x++) {
			auto this_val = data[(area.x + x) + (area.y + y) * width];
			data[(area.x + x) + (area.y + y) * width] = RGBA(
					(uint8_t)((premultiplied_r + inv_alpha * COLOR_R(this_val)) >> 8),
					(uint8_t)((premultiplied_g + inv_alpha * COLOR_G(this_val)) >> 8),
					(uint8_t)((premultiplied_b + inv_alpha * COLOR_B(this_val)) >> 8),
					(uint8_t)((premultiplied_a + inv_alpha * COLOR_A(this_val)) >> 8));
		}
	}
}

void Framebuffer::fill_gradient_h(Rect area, Color color_a, Color color_b) const {
	if(color_a == color_b)
		fill(area, color_a);
	for(int x = 0; x < area.width; x++)
		fill({area.x + x, area.y, 1, area.height}, color_a.mixed(color_b, (float) x / area.width));
}

void Framebuffer::fill_gradient_v(Rect area, Color color_a, Color color_b) const {
	if(color_a == color_b)
		fill(area, color_a);
	for(int y = 0; y < area.height; y++)
		fill({area.x, area.y + y, area.width, 1}, color_a.mixed(color_b, (float) y / area.height));
}

void Framebuffer::invert(Gfx::Rect area) const {
	//Make sure area is in the bounds of the framebuffer
	area = area.overlapping_area({0, 0, width, height});
	if(area.empty())
		return;

	for(int y = 0; y < area.height; y++) {
		for(int x = 0; x < area.width; x++) {
			auto& color = data[x + area.x + (area.y + y) * width];
			color = color.inverted();
		}
	}
}

void Framebuffer::invert_checkered(Gfx::Rect area) const {
	//Make sure area is in the bounds of the framebuffer
	area = area.overlapping_area({0, 0, width, height});
	if(area.empty())
		return;

	for(int y = 0; y < area.height; y++) {
		for(int x = y % 2; x < area.width; x+=2) {
			auto& color = data[x + area.x + (area.y + y) * width];
			color = color.inverted();
		}
	}
}

void Framebuffer::outline(Rect area, Color color) const {
	fill({area.x + 1, area.y, area.width - 2, 1}, color);
	fill({area.x + 1, area.y + area.height - 1, area.width - 2, 1}, color);
	fill({area.x, area.y, 1, area.height}, color);
	fill({area.x + area.width - 1, area.y, 1, area.height}, color);
}

void Framebuffer::outline_blitting(Rect area, Color color) const {
	fill_blitting({area.x + 1, area.y, area.width - 2, 1}, color);
	fill_blitting({area.x + 1, area.y + area.height - 1, area.width - 2, 1}, color);
	fill_blitting({area.x, area.y, 1, area.height}, color);
	fill_blitting({area.x + area.width - 1, area.y, 1, area.height}, color);
}

void Framebuffer::outline_inverting(Gfx::Rect area) const {
	invert({area.x + 1, area.y, area.width - 2, 1});
	invert({area.x + 1, area.y + area.height - 1, area.width - 2, 1});
	invert({area.x, area.y, 1, area.height});
	invert({area.x + area.width - 1, area.y, 1, area.height});
}

void Framebuffer::outline_inverting_checkered(Gfx::Rect area) const {
	invert_checkered({area.x + 1, area.y, area.width - 2, 1});
	invert_checkered({area.x + 1, area.y + area.height - 1, area.width - 2, 1});
	invert_checkered({area.x, area.y, 1, area.height});
	invert_checkered({area.x + area.width - 1, area.y, 1, area.height});
}

void Framebuffer::draw_text(const char* str, const Point& pos, Font* font, Color color) const {
	Point current_pos = pos;
	while(*str) {
		current_pos = draw_glyph(font, *str, current_pos, color);
		str++;
	}
}

Point Framebuffer::draw_glyph(Font* font, uint32_t codepoint, const Point& glyph_pos, Color color) const {
	auto* glyph = font->glyph(codepoint);
	int y_offset = (font->bounding_box().base_y - glyph->base_y) + (font->size() - glyph->height);
	int x_offset = glyph->base_x - font->bounding_box().base_x;
	Point pos = {glyph_pos.x + x_offset, glyph_pos.y + y_offset};
	Rect glyph_area = {0, 0, glyph->width, glyph->height};

	//Calculate the color multipliers
	double r_mult = COLOR_R(color) / 255.0;
	double g_mult = COLOR_G(color) / 255.0;
	double b_mult = COLOR_B(color) / 255.0;
	double alpha_mult = COLOR_A(color) / 255.0;

	//Make sure self_area is in bounds of the framebuffer
	Rect self_area = {pos.x, pos.y, glyph_area.width, glyph_area.height};
	self_area = self_area.overlapping_area({0, 0, width, height});
	if(self_area.empty())
		return glyph_pos + Point {glyph->next_offset.x, glyph->next_offset.y};

	//Update glyph_area with the changes made to self_area
	glyph_area.x += self_area.x - pos.x;
	glyph_area.y += self_area.y - pos.y;
	glyph_area.width = self_area.width;
	glyph_area.height = self_area.height;

	for(int y = 0; y < self_area.height; y++) {
		for(int x = 0; x < self_area.width; x++) {
			auto& this_val = data[(self_area.x + x) + (self_area.y + y) * width];
			auto& other_val = glyph->bitmap[(glyph_area.x + x) + (glyph_area.y + y) * glyph->width];
			double alpha = (COLOR_A(other_val) / 255.00) * alpha_mult;
			if(alpha == 0)
				continue;
			double oneminusalpha = 1.00 - alpha;
			this_val = RGB(
					(uint8_t) (COLOR_R(this_val) * oneminusalpha + COLOR_R(other_val) * alpha * r_mult),
					(uint8_t) (COLOR_G(this_val) * oneminusalpha + COLOR_G(other_val) * alpha * g_mult),
					(uint8_t) (COLOR_B(this_val) * oneminusalpha + COLOR_B(other_val) * alpha * b_mult));
		}
	}

	return glyph_pos + Point {glyph->next_offset.x, glyph->next_offset.y};
}

void Framebuffer::multiply(Color color) {
	for(int y = 0; y < height; y++) {
		for(int x = 0; x < width; x++) {
			data[x + y * width] *= color;
		}
	}
}

Color* Framebuffer::at(const Point& position) const {
	if(position.x < 0 || position.y < 0 || position.y >= height || position.x >= width)
		return NULL;

	return &data[position.x + position.y * width];
}

struct FramebufferSerialization {
	int width;
	int height;
	uint32_t data[];
};

size_t Framebuffer::serialized_size() const {
	return sizeof(FramebufferSerialization) + width * height * sizeof(uint32_t);
}

void Framebuffer::serialize(uint8_t*& buf) const {
	auto* serialization = (FramebufferSerialization*) buf;
	serialization->width = data ? width : 0;
	serialization->height = data ? height : 0;
	if(data && width && height)
		memcpy_uint32(serialization->data, (uint32_t*) data, width * height);
	buf += serialized_size();
}

void Framebuffer::deserialize(const uint8_t*& buf) {
	delete data;
	auto* serialization = (FramebufferSerialization*) buf;
	width = serialization->width;
	height = serialization->height;
	if(width && height) {
		data = (Color*) malloc(sizeof(Color) * width * height);
		memcpy_uint32((uint32_t*) data, serialization->data, width * height);
	}
	should_free = true;
	buf += serialized_size();
}

void Framebuffer::put(Gfx::Point point, Gfx::Color color) const {
	if(point.x >= width || point.y >= height || point.x < 0 || point.y < 0)
		return;
	ref_at(point) = color;
}
