#include "C3DVertexDeclaration.h"
#include "C3DMesh.h"
#include "C3DEffect.h"
#include "C3DVertexFormat.h"
#include "Base.h"
#include "C3DDeviceAdapter.h"

// Graphics (GLSL)
#define VERTEX_ATTRIBUTE_POSITION_NAME              "a_position"
#define VERTEX_ATTRIBUTE_NORMAL_NAME                "a_normal"
#define VERTEX_ATTRIBUTE_COLOR_NAME                 "a_color"
#define VERTEX_ATTRIBUTE_TANGENT_NAME               "a_tangent"
#define VERTEX_ATTRIBUTE_BINORMAL_NAME              "a_binormal"
#define VERTEX_ATTRIBUTE_BLENDWEIGHTS_NAME          "a_blendWeights"
#define VERTEX_ATTRIBUTE_BLENDINDICES_NAME          "a_blendIndices"
#define VERTEX_ATTRIBUTE_TEXCOORD_PREFIX_NAME       "a_texCoord"

namespace cocos3d
{
	static GLuint __maxVertexAttribs = 0;
	static std::vector<C3DVertexDeclaration*> __vertexAttributeBindingCache;

	static int __curvaEnableMask = 0;

	C3DVertexDeclaration::C3DVertexDeclaration() :
		_handle(0), _attributes(NULL), _mesh(NULL), _effect(NULL), _vaEnableMask(0)
	{
	}

	C3DVertexDeclaration::~C3DVertexDeclaration()
	{
		// Delete from the vertex attribute binding cache.
		std::vector<C3DVertexDeclaration*>::iterator itr = std::find(__vertexAttributeBindingCache.begin(), __vertexAttributeBindingCache.end(), this);
		if (itr != __vertexAttributeBindingCache.end())
		{
			__vertexAttributeBindingCache.erase(itr);
		}

		SAFE_RELEASE(_mesh);

		SAFE_DELETE_ARRAY(_attributes);

		if(C3DDeviceAdapter::getInstance()->isSupportVAO())
		{
			if (_handle)
			{
				/*GL_ASSERT*/( glDeleteVertexArrays(1, &_handle) );
				_handle = 0;
			}
		}
	}

	C3DVertexDeclaration* C3DVertexDeclaration::create(C3DMesh* mesh, C3DEffect* effect)
	{
		// Search for an existing vertex attribute binding that can be used.
		C3DVertexDeclaration* b;
		for (unsigned int i = 0, count = __vertexAttributeBindingCache.size(); i < count; ++i)
		{
			b = __vertexAttributeBindingCache[i];
			if (b->_mesh == mesh && b->_effect == effect)
			{
				// Found a match!
				b->retain();
				return b;
			}
		}

		//b = create(mesh, mesh->getVertexFormat(), 0, effect);
		//-----------------------------------------------------------------------------------
		b = new C3DVertexDeclaration();

		if( b->init(mesh, mesh->getVertexFormat(), 0, effect) )
		{
			// Add the new vertex attribute binding to the cache.
			__vertexAttributeBindingCache.push_back(b);
			return b;
		}

		SAFE_RELEASE(b);
		return NULL;
	}

	C3DVertexDeclaration* C3DVertexDeclaration::create(const C3DVertexFormat* vertexFormat, void* vertexPointer, C3DEffect* effect)
	{
		C3DVertexDeclaration* dec = new C3DVertexDeclaration();
		if(dec->init(NULL, vertexFormat, vertexPointer, effect))
		{
			return dec;
		}
		else
		{
			SAFE_RELEASE(dec);
			return NULL;
		}
	}

	void C3DVertexDeclaration::reload()
	{
		LOG_TRACE("     C3DVertexDeclaration begin reload");
		if(_mesh)
		{
			C3DMesh* mesh = _mesh;
			SAFE_DELETE_ARRAY(_attributes);

			init(mesh, mesh->getVertexFormat(), 0, _effect);

			SAFE_RELEASE(mesh);
		}
	}

	bool C3DVertexDeclaration::init(C3DMesh* mesh, const C3DVertexFormat* vertexFormat, void* vertexPointer, C3DEffect* effect)
	{
		// One-time initialization.
		if (__maxVertexAttribs == 0)
		{
			GLint temp;
			GL_ASSERT( glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &temp) );

			__maxVertexAttribs = temp;
			assert(__maxVertexAttribs > 0);
			if (__maxVertexAttribs <= 0)
			{
				return false;
			}
		}

		// configure C3DVertexDeclaration.
		if(C3DDeviceAdapter::getInstance()->isSupportVAO())
		{
			if (mesh && glGenVertexArrays)
			{
				GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, 0) );
				GL_ASSERT( glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) );

				// Use hardware VAOs.
				GL_ASSERT( glGenVertexArrays(1, &_handle) );

				if (_handle == 0)
				{
					return false;
				}

				// Bind the new VAO.
				GL_ASSERT( glBindVertexArray(_handle) );

				// Bind the C3DMesh VBO so our glVertexAttribPointer calls use it.
				GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, mesh->getVertexBuffer()) );
			}
			else
			{
				// Construct a software representation of a VAO.
				C3DVertexDeclaration::C3DVertexAttribute* attribs = new C3DVertexDeclaration::C3DVertexAttribute[__maxVertexAttribs];
				for (unsigned int i = 0; i < __maxVertexAttribs; ++i)
				{
					// Set GL defaults
					attribs[i].enabled = GL_FALSE;
					attribs[i].size = 4;
					attribs[i].stride = 0;
					attribs[i].type = GL_FLOAT;
					attribs[i].normalized = GL_FALSE;
					attribs[i].pointer = 0;
				}
				_attributes = attribs;
			}
		}
		else
		{
			// Construct a software representation of a VAO.
			C3DVertexDeclaration::C3DVertexAttribute* attribs = new C3DVertexDeclaration::C3DVertexAttribute[__maxVertexAttribs];
			for (unsigned int i = 0; i < __maxVertexAttribs; ++i)
			{
				// Set GL defaults
				attribs[i].enabled = GL_FALSE;
				attribs[i].size = 4;
				attribs[i].stride = 0;
				attribs[i].type = GL_FLOAT;
				attribs[i].normalized = GL_FALSE;
				attribs[i].pointer = 0;
			}
			_attributes = attribs;
		}

		if (mesh)
		{
			_mesh = mesh;
			mesh->retain();
		}

		_effect = effect;

		// Call setVertexAttribPointer for each vertex element.
		std::string name;
		unsigned int offset = 0;
		for (unsigned int i = 0, count = vertexFormat->getElementCount(); i < count; ++i)
		{
			const C3DVertexElement* elem = vertexFormat->getElement(i);

			cocos3d::VertexAttribute attrib;

			// Constructor vertex attribute name expected in shader.
			switch (elem->usage)
			{
			case Vertex_Usage_POSITION:
				attrib = effect->getVertexAttribute(VERTEX_ATTRIBUTE_POSITION_NAME);
				break;
			case Vertex_Usage_NORMAL:
				attrib = effect->getVertexAttribute(VERTEX_ATTRIBUTE_NORMAL_NAME);
				break;
			case Vertex_Usage_COLOR:
				attrib = effect->getVertexAttribute(VERTEX_ATTRIBUTE_COLOR_NAME);
				break;
			case Vertex_Usage_TANGENT:
				attrib = effect->getVertexAttribute(VERTEX_ATTRIBUTE_TANGENT_NAME);
				break;
			case Vertex_Usage_BINORMAL:
				attrib = effect->getVertexAttribute(VERTEX_ATTRIBUTE_BINORMAL_NAME);
				break;
			case Vertex_Usage_BLENDWEIGHTS:
				attrib = effect->getVertexAttribute(VERTEX_ATTRIBUTE_BLENDWEIGHTS_NAME);
				break;
			case Vertex_Usage_BLENDINDICES:
				attrib = effect->getVertexAttribute(VERTEX_ATTRIBUTE_BLENDINDICES_NAME);
				break;
			case Vertex_Usage_TEXCOORD0:
				attrib = effect->getVertexAttribute(VERTEX_ATTRIBUTE_TEXCOORD_PREFIX_NAME);
				// Try adding a "0" after the texcoord attrib name (flexible name for this case).
				if (attrib == -1)
				{
					name = VERTEX_ATTRIBUTE_TEXCOORD_PREFIX_NAME;
					name += "0";
					attrib = effect->getVertexAttribute(name);
				}
				break;
			case Vertex_Usage_TEXCOORD1:
			case Vertex_Usage_TEXCOORD2:
			case Vertex_Usage_TEXCOORD3:
			case Vertex_Usage_TEXCOORD4:
			case Vertex_Usage_TEXCOORD5:
			case Vertex_Usage_TEXCOORD6:
			case Vertex_Usage_TEXCOORD7:
				name = VERTEX_ATTRIBUTE_TEXCOORD_PREFIX_NAME;
				name += (elem->usage - Vertex_Usage_TEXCOORD0) + '0';
				attrib = effect->getVertexAttribute(name);
				break;
			default:
				attrib = -1;
				break;
			}

			if (attrib == -1)
			{
				
			//	WARN_VARG("Warning: Vertex element with usage '%s' in mesh '%s' does not correspond to an attribute in effect '%s'.", C3DVertexFormat::toString(elem->usage), mesh->getUrl(), effect->getID());
			}
			else
			{
				void* pointer = vertexPointer ? (void*)(((unsigned char*)vertexPointer) + offset) : (void*)offset;
				setVertexAttribPointer(attrib, (GLint)elem->size, GL_FLOAT, GL_FALSE, (GLsizei)vertexFormat->getVertexSize(), pointer);
			}

			offset += elem->size * sizeof(float);
		}

		if(C3DDeviceAdapter::getInstance()->isSupportVAO())
		{
			if (_handle)
			{
				GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, 0) );
				GL_ASSERT( glBindVertexArray(0) );
			}
		}

		return true;
	}

	void C3DVertexDeclaration::setVertexAttribPointer(GLuint indx, GLint size, GLenum type, GLboolean normalize, GLsizei stride, void* pointer)
	{
		assert(indx < (GLuint)__maxVertexAttribs);

		//_vaEnableMask |= 1 << indx;

		if (_handle)
		{
			// Hardware mode
			GL_ASSERT( glEnableVertexAttribArray(indx) );
			GL_ASSERT( glVertexAttribPointer(indx, size, type, normalize, stride, pointer) );
		}
		else
		{
			// Software mode
			_attributes[indx].enabled = true;
			_attributes[indx].size = size;
			_attributes[indx].type = type;
			_attributes[indx].normalized = normalize;
			_attributes[indx].stride = stride;
			_attributes[indx].pointer = pointer;
		}
	}

	void C3DVertexDeclaration::bind()
	{
		if( C3DDeviceAdapter::getInstance()->isSupportVAO() == true)
		{
			if (_handle)
			{
				// Hardware mode
				GL_ASSERT( glBindVertexArray(_handle) );
			}
			else
			{
				// Software mode
				if (_mesh)
				{
					GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, _mesh->getVertexBuffer()) );
				}
				else
				{
					GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, 0) );
				}

				for (unsigned int i = 0; i < __maxVertexAttribs; ++i)
				{
					C3DVertexAttribute& attr = _attributes[i];
					if (attr.enabled)
					{
						GL_ASSERT( glVertexAttribPointer(i, attr.size, attr.type, attr.normalized, attr.stride, attr.pointer) );
						GL_ASSERT( glEnableVertexAttribArray(i) );
					}
				}
			}
		}
		else
		{
			// Software mode
			if (_mesh)
			{
				GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, _mesh->getVertexBuffer()) );
			}
			else
			{
				GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, 0) );
			}

			for (unsigned int i = 0; i < __maxVertexAttribs; ++i)
			{
				C3DVertexAttribute& a = _attributes[i];
				if (a.enabled)
				{
					GL_ASSERT( glVertexAttribPointer(i, a.size, a.type, a.normalized, a.stride, a.pointer) );
					GL_ASSERT( glEnableVertexAttribArray(i) );
				}
			}
		}
	}

	void C3DVertexDeclaration::unbind()
	{
		if(C3DDeviceAdapter::getInstance()->isSupportVAO())
		{
			if (_handle)
			{
				// Hardware mode
				GL_ASSERT( glBindVertexArray(0) );
			}
			else
			{
				// Software mode
				if (_mesh)
				{
					GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, 0) );
				}

				for (unsigned int i = 0; i < __maxVertexAttribs; ++i)
				{
					if (_attributes[i].enabled)
					{
						GL_ASSERT( glDisableVertexAttribArray(i) );
					}
				}
			}
		}
		else
		{
			// Software mode
			if (_mesh)
			{
				GL_ASSERT( glBindBuffer(GL_ARRAY_BUFFER, 0) );
			}

			for (unsigned int i = 0; i < __maxVertexAttribs; ++i)
			{
				if (_attributes[i].enabled)
				{
					GL_ASSERT( glDisableVertexAttribArray(i) );
				}
			}
		}
	}

	int C3DVertexDeclaration::getCurVertAttEnables()
	{
		return __curvaEnableMask;
	}

	void C3DVertexDeclaration::setCurVertAttEnables(int enableMask, bool force)
	{
		if (!force)
		{
			int mask = __curvaEnableMask ^ enableMask;

			for (size_t i = 0; i < __maxVertexAttribs; i++)
			{
				if (mask & (1 << i))
				{
					if (__curvaEnableMask & (1 << i))
					{
						GL_ASSERT(glDisableVertexAttribArray(i));
					}
					else
					{
						GL_ASSERT(glEnableVertexAttribArray(i));
					}
				}
			}
		}
		else
		{
			for (size_t i = 0; i < __maxVertexAttribs; i++)
			{
				if (enableMask & (1 << i))
				{
					GL_ASSERT(glEnableVertexAttribArray(i));
				}
				else
				{
					GL_ASSERT(glDisableVertexAttribArray(i));
				}
			}
		}
		__curvaEnableMask = enableMask;
	}
}