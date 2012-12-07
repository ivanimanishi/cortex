//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2007-2012, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include <iostream>

#include "IECore/SplineData.h"
#include "IECore/MessageHandler.h"

#include "IECoreGL/ShaderStateComponent.h"
#include "IECoreGL/Shader.h"
#include "IECoreGL/ShaderLoader.h"
#include "IECoreGL/Texture.h"
#include "IECoreGL/TextureLoader.h"
#include "IECoreGL/CachedConverter.h"

using namespace std;
using namespace IECoreGL;

StateComponent::Description<ShaderStateComponent> ShaderStateComponent::g_description;

ShaderStateComponent::ShaderStateComponent()
	:	m_shaderLoader( 0 ), m_textureLoader( 0 ), m_fragmentShader( "" ), m_vertexShader( "" ), 
		m_parameterMap( 0 ), m_shaderSetup( 0 )
{
}

ShaderStateComponent::ShaderStateComponent( ShaderLoaderPtr shaderLoader, TextureLoaderPtr textureLoader, const std::string vertexShader, const std::string fragmentShader, IECore::ConstCompoundObjectPtr parameterValues ) :
	m_shaderLoader( shaderLoader ), m_textureLoader( textureLoader ), m_fragmentShader( fragmentShader ), 
	m_vertexShader( vertexShader ), m_parameterMap( parameterValues->copy() ), m_shaderSetup( 0 )
{
}

void ShaderStateComponent::bind() const
{
}

Shader::Setup *ShaderStateComponent::shaderSetup()
{
	ensureShaderSetup();
	return m_shaderSetup;
}

const Shader::Setup *ShaderStateComponent::shaderSetup() const
{
	ensureShaderSetup();
	return m_shaderSetup;
}

void ShaderStateComponent::ensureShaderSetup() const
{
	if( m_shaderSetup )
	{
		return;
	}

	if( !m_shaderLoader )
	{
		// we were default constructed, so we're just a facing ratio shader.
		m_shaderSetup = new Shader::Setup( Shader::facingRatio() );
		return;
	}

	// load a shader, create a setup, and add our parameters to it.
	ShaderPtr shader = m_shaderLoader->create( m_vertexShader, m_fragmentShader );
	m_shaderSetup = new Shader::Setup( shader );

	const IECore::CompoundObject::ObjectMap &d = m_parameterMap->members();
	for( IECore::CompoundObject::ObjectMap::const_iterator it = d.begin(), eIt = d.end(); it != eIt; it++ )
	{
		GLenum type;
		GLint size;
		size_t textureUnit;
		if( shader->uniformParameter( it->first, type, size, textureUnit ) == -1 )
		{
			// parameter doesn't exist
			continue;
		}
		
		if( type == GL_SAMPLER_2D )
		{
			ConstTexturePtr texture = 0;
			if(
				it->second->typeId() == IECore::ImagePrimitiveTypeId ||
				it->second->typeId() == IECore::CompoundDataTypeId ||
				it->second->typeId() == IECore::SplineffData::staticTypeId() ||
				it->second->typeId() == IECore::SplinefColor3fData::staticTypeId()
			)
			{
				texture = IECore::runTimeCast<const Texture>( CachedConverter::defaultCachedConverter()->convert( it->second ) );
			}
			else if( it->second->typeId() == IECore::StringData::staticTypeId() )
			{
				const std::string &fileName = static_cast<const IECore::StringData *>( it->second.get() )->readable();
				if( fileName!="" )
				{
					texture = m_textureLoader->load( fileName );
				}
			}
			
			m_shaderSetup->addUniformParameter( it->first.value(), texture );
		}
		else if( it->second->isInstanceOf( IECore::DataTypeId ) )
		{
			m_shaderSetup->addUniformParameter( it->first.value(), IECore::staticPointerCast<const IECore::Data>( it->second ) );
		}
	}
}
