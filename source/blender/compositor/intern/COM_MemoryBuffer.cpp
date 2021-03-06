/*
 * Copyright 2011, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor: 
 *		Jeroen Bakker 
 *		Monique Dewanchand
 */

#include "COM_MemoryBuffer.h"

#include "MEM_guardedalloc.h"

using std::min;
using std::max;

unsigned int MemoryBuffer::determineBufferSize()
{
	return getWidth() * getHeight();
}

int MemoryBuffer::getWidth() const
{
	return this->m_rect.xmax - this->m_rect.xmin;
}
int MemoryBuffer::getHeight() const
{
	return this->m_rect.ymax - this->m_rect.ymin;
}

MemoryBuffer::MemoryBuffer(MemoryProxy *memoryProxy, unsigned int chunkNumber, rcti *rect)
{
	BLI_rcti_init(&this->m_rect, rect->xmin, rect->xmax, rect->ymin, rect->ymax);
	this->m_memoryProxy = memoryProxy;
	this->m_chunkNumber = chunkNumber;
	this->m_buffer = (float *)MEM_mallocN_aligned(sizeof(float) * determineBufferSize() * COM_NUMBER_OF_CHANNELS, 16, "COM_MemoryBuffer");
	this->m_state = COM_MB_ALLOCATED;
	this->m_datatype = COM_DT_COLOR;
	this->m_chunkWidth = this->m_rect.xmax - this->m_rect.xmin;
}

MemoryBuffer::MemoryBuffer(MemoryProxy *memoryProxy, rcti *rect)
{
	BLI_rcti_init(&this->m_rect, rect->xmin, rect->xmax, rect->ymin, rect->ymax);
	this->m_memoryProxy = memoryProxy;
	this->m_chunkNumber = -1;
	this->m_buffer = (float *)MEM_mallocN_aligned(sizeof(float) * determineBufferSize() * COM_NUMBER_OF_CHANNELS, 16, "COM_MemoryBuffer");
	this->m_state = COM_MB_TEMPORARILY;
	this->m_datatype = COM_DT_COLOR;
	this->m_chunkWidth = this->m_rect.xmax - this->m_rect.xmin;
}
MemoryBuffer *MemoryBuffer::duplicate()
{
	MemoryBuffer *result = new MemoryBuffer(this->m_memoryProxy, &this->m_rect);
	memcpy(result->m_buffer, this->m_buffer, this->determineBufferSize() * COM_NUMBER_OF_CHANNELS * sizeof(float));
	return result;
}
void MemoryBuffer::clear()
{
	memset(this->m_buffer, 0, this->determineBufferSize() * COM_NUMBER_OF_CHANNELS * sizeof(float));
}

float *MemoryBuffer::convertToValueBuffer()
{
	const unsigned int size = this->determineBufferSize();
	unsigned int i;

	float *result = (float *)MEM_mallocN(sizeof(float) * size, __func__);

	const float *fp_src = this->m_buffer;
	float       *fp_dst = result;

	for (i = 0; i < size; i++, fp_dst++, fp_src += COM_NUMBER_OF_CHANNELS) {
		*fp_dst = *fp_src;
	}

	return result;
}

float MemoryBuffer::getMaximumValue()
{
	float result = this->m_buffer[0];
	const unsigned int size = this->determineBufferSize();
	unsigned int i;

	const float *fp_src = this->m_buffer;

	for (i = 0; i < size; i++, fp_src += COM_NUMBER_OF_CHANNELS) {
		float value = *fp_src;
		if (value > result) {
			result = value;
		}
	}

	return result;
}

float MemoryBuffer::getMaximumValue(rcti *rect)
{
	rcti rect_clamp;

	/* first clamp the rect by the bounds or we get un-initialized values */
	BLI_rcti_isect(rect, &this->m_rect, &rect_clamp);

	if (!BLI_rcti_is_empty(&rect_clamp)) {
		MemoryBuffer *temp = new MemoryBuffer(NULL, &rect_clamp);
		temp->copyContentFrom(this);
		float result = temp->getMaximumValue();
		delete temp;
		return result;
	}
	else {
		BLI_assert(0);
		return 0.0f;
	}
}

MemoryBuffer::~MemoryBuffer()
{
	if (this->m_buffer) {
		MEM_freeN(this->m_buffer);
		this->m_buffer = NULL;
	}
}

void MemoryBuffer::copyContentFrom(MemoryBuffer *otherBuffer)
{
	if (!otherBuffer) {
		BLI_assert(0);
		return;
	}
	unsigned int otherY;
	unsigned int minX = max(this->m_rect.xmin, otherBuffer->m_rect.xmin);
	unsigned int maxX = min(this->m_rect.xmax, otherBuffer->m_rect.xmax);
	unsigned int minY = max(this->m_rect.ymin, otherBuffer->m_rect.ymin);
	unsigned int maxY = min(this->m_rect.ymax, otherBuffer->m_rect.ymax);
	int offset;
	int otherOffset;


	for (otherY = minY; otherY < maxY; otherY++) {
		otherOffset = ((otherY - otherBuffer->m_rect.ymin) * otherBuffer->m_chunkWidth + minX - otherBuffer->m_rect.xmin) * COM_NUMBER_OF_CHANNELS;
		offset = ((otherY - this->m_rect.ymin) * this->m_chunkWidth + minX - this->m_rect.xmin) * COM_NUMBER_OF_CHANNELS;
		memcpy(&this->m_buffer[offset], &otherBuffer->m_buffer[otherOffset], (maxX - minX) * COM_NUMBER_OF_CHANNELS * sizeof(float));
	}
}

void MemoryBuffer::writePixel(int x, int y, const float color[4])
{
	if (x >= this->m_rect.xmin && x < this->m_rect.xmax &&
	    y >= this->m_rect.ymin && y < this->m_rect.ymax)
	{
		const int offset = (this->m_chunkWidth * (y - this->m_rect.ymin) + x - this->m_rect.xmin) * COM_NUMBER_OF_CHANNELS;
		copy_v4_v4(&this->m_buffer[offset], color);
	}
}

void MemoryBuffer::addPixel(int x, int y, const float color[4])
{
	if (x >= this->m_rect.xmin && x < this->m_rect.xmax &&
	    y >= this->m_rect.ymin && y < this->m_rect.ymax)
	{
		const int offset = (this->m_chunkWidth * (y - this->m_rect.ymin) + x - this->m_rect.xmin) * COM_NUMBER_OF_CHANNELS;
		add_v4_v4(&this->m_buffer[offset], color);
	}
}

typedef struct ReadEWAData {
	MemoryBuffer *buffer;
	PixelSampler sampler;
	float ufac, vfac;
} ReadEWAData;

static void read_ewa_pixel_sampled(void *userdata, int x, int y, float result[4])
{
	ReadEWAData *data = (ReadEWAData *) userdata;
	switch (data->sampler) {
		case COM_PS_NEAREST:
			data->buffer->read(result, x, y);
			break;
		case COM_PS_BILINEAR:
			data->buffer->readBilinear(result,
			                           (float)x + data->ufac,
			                           (float)y + data->vfac);
			break;
		case COM_PS_BICUBIC:
			/* TOOD(sergey): no readBicubic method yet */
			data->buffer->readBilinear(result,
			                           (float)x + data->ufac,
			                           (float)y + data->vfac);
			break;
		default:
			zero_v4(result);
			break;
	}
}

void MemoryBuffer::readEWA(float result[4], const float uv[2], const float derivatives[2][2], PixelSampler sampler)
{
	ReadEWAData data;
	data.buffer = this;
	data.sampler = sampler;
	data.ufac = uv[0] - floorf(uv[0]);
	data.vfac = uv[1] - floorf(uv[1]);

	int width = this->getWidth(), height = this->getHeight();
	/* TODO(sergey): Render pipeline uses normalized coordinates and derivatives,
	 * but compositor uses pixel space. For now let's just divide the values and
	 * switch compositor to normalized space for EWA later.
	 */
	float uv_normal[2] = {uv[0] / width, uv[1] / height};
	float du_normal[2] = {derivatives[0][0] / width, derivatives[0][1] / height};
	float dv_normal[2] = {derivatives[1][0] / width, derivatives[1][1] / height};

	BLI_ewa_filter(this->getWidth(), this->getHeight(),
	               false,
	               true,
	               uv_normal, du_normal, dv_normal,
	               read_ewa_pixel_sampled,
	               &data,
	               result);
}
