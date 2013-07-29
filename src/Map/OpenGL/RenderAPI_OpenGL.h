/**
 * @file
 *
 * @section LICENSE
 *
 * OsmAnd - Android navigation software based on OSM maps.
 * Copyright (C) 2010-2013  OsmAnd Authors listed in AUTHORS file
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __RENDER_API__OPENGL_H_
#define __RENDER_API__OPENGL_H_

#include <stdint.h>
#include <memory>
#include <array>

#include <glm/glm.hpp>

#include <OsmAndCore.h>
#include <OsmAndCore/CommonTypes.h>
#include <OpenGL_Common/RenderAPI_OpenGL_Common.h>

namespace OsmAnd {

    class OSMAND_CORE_API RenderAPI_OpenGL : public RenderAPI_OpenGL_Common
    {
    private:
    protected:
        virtual void glTexStorage2D_wrapper(GLenum target, GLsizei levels, GLsizei width, GLsizei height, GLenum sourceFormat, GLenum sourcePixelDataType);
        virtual GLenum validateResult();
        
        GLuint _textureSampler_Bitmap_NoAtlas;
        GLuint _textureSampler_Bitmap_Atlas;
        GLuint _textureSampler_ElevationData_NoAtlas;
        GLuint _textureSampler_ElevationData_Atlas;
    public:
        RenderAPI_OpenGL();
        virtual ~RenderAPI_OpenGL();

        virtual bool initialize(const uint32_t& optimalTilesPerAtlasSqrt);
        virtual bool release();

        const GLuint& textureSampler_Bitmap_NoAtlas;
        const GLuint& textureSampler_Bitmap_Atlas;
        const GLuint& textureSampler_ElevationData_NoAtlas;
        const GLuint& textureSampler_ElevationData_Atlas;
    };

}

#endif // __RENDER_API__OPENGL_H_
