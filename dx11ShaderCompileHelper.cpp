// ==========================================================================
// Copyright 1995,2006,2008 Autodesk, Inc. All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk
// license agreement provided at the time of installation or download,
// or which otherwise accompanies this software in either electronic
// or hard copy form.
// ==========================================================================
//+

#if _MSC_VER >= 1700
#pragma warning( disable: 4005 )
#endif

#include <maya/MString.h>
#include <maya/MGlobal.h>
#include <maya/MFileObject.h>
#include <maya/MSceneMessage.h>

#include "dx11ShaderCompileHelper.h"
#include "dx11ShaderStrings.h"

// Includes for DX11
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#if _MSC_VER < 1700
#include <d3dx11.h>
#endif

// To build against the DX SDK header use the following commented line
//#include <../Samples/C++/Effects11/Inc/d3dx11effect.h>
#include <maya/d3dx11effect.h>
#include <d3dcompiler.h>

#include <sys/stat.h>
#include <string.h>
#include <map>
#include <set>
#include <list>

/*!
	CDX11EffectCompileHelper::EffectCollection
	Is a collection of the all effects currently used by the scene.
	When a .fx file is loaded, a look up is done in the collection for a compiled version,
	if found it will be cloned and added to the collection.
	It has to be cloned because each effect need to have distinct parameters.

	CDX11EffectCompileHelper::CompiledEffectCache
	Is a simple LRU for the last 8 compiled effects.
	Since the collection above is only used to store the effects currently active in the scene,
	this LRU is used to prevent reloading over and over the MayaUberShader which is assigned by default on each new dx11Shader.
	2 callbacks are registered to flush the LRU when the scene is closed and when maya is about to close:
	MsceneMessage::addCallback(MSceneMessage::kMayaExiting)
	MsceneMessage::addCallback(MSceneMessage::kBeforeNew)
*/

namespace CDX11EffectCompileHelper
{
	class CFileReferenceHelper
	{
	public:
		MString resolveFileName(const char* fileName) const;
		void setReferencePath(MString fileName);

	protected:
		MString findFile(const char* fileName) const;
		MString getSearchPaths() const;

		MString referencePath;
	};

	MString CFileReferenceHelper::resolveFileName(const char* fileName) const
	{
		//Check if filename exists
		MString currFileName(fileName);
		MString file = findFile(currFileName.asChar());

		int hasFile = file.length() > 0;

		if (hasFile == 0)
		{
			// lets extract the filename and try it again...
			int idx = currFileName.rindex('/');
			if (idx == -1)
				idx = currFileName.rindex('\\');
			if (idx != -1)
			{
				currFileName = currFileName.substring(idx+1,currFileName.length()-1);
				file = findFile(currFileName.asChar());
			}
		}

		if (file.length() == 0)
		{
			MString expandedFileName(MString(fileName).expandEnvironmentVariablesAndTilde());
			file = findFile(expandedFileName.asChar());
		}

		return file;
	}

	void CFileReferenceHelper::setReferencePath(MString fileName)
	{
		referencePath.clear();
		// split file path in filename path
		// lets extract the filename and try it again...
		int idx = fileName.rindex('/');
		if (idx == -1)
			idx = fileName.rindex('\\');
		if (idx != -1)
		{
			referencePath  = fileName.substring(0,idx);
		}
	}

	MString CFileReferenceHelper::findFile(const char* fileName) const
	{
		struct stat statBuf;
		MString name (fileName);
		const bool fullyQualified = name.index('/') == 0 || name.index('\\') == 0 || name.index(':') == 1;
		if (fullyQualified && stat(name.asChar(), &statBuf) != -1) 
		{
			return name;
		}
		
		char path[MAX_PATH];
		MString searchPaths = getSearchPaths();
		const char * psearchpath = searchPaths.asChar();

		/// Strip out any leading '/' at this point since the file has
		// not been found using a fully qualified path.
		MString resolvedName;
		if(name.index('/') == 0 || name.index('\\') == 0)
			resolvedName = name.substring(1, name.length() - 1);
		else
			resolvedName = name;

		while (psearchpath < searchPaths.asChar() + searchPaths.length())
		{
			const char * endpath = strchr(psearchpath,';');
			if (endpath)
			{
				strncpy(path,psearchpath, endpath - psearchpath);
				path[endpath - psearchpath] = '\0';
			}
			else
			{
				strcpy(path,psearchpath);
			}

			psearchpath += strlen(path)+1;

			bool fullPath = (path[0] == '/'	|| path[0] == '\\');

			if (strlen(path) > 2)
			{
				fullPath = fullPath ||
					(path[1] == ':' &&
					(path[2] == '/' ||
					path[2] == '\\'));
			}

			// Add the path and the filename together to get the full path
			MString file;
			if(path[strlen(path) - 1] != '/')
				file = MString(path) + "/" + resolvedName;
			else
				file = MString(path) + resolvedName;

		
			if (stat(file.asChar(), &statBuf) != -1) 
			{
				return file;
			}
		}
		return MString();
	}

	MString CFileReferenceHelper::getSearchPaths() const
	{
		// Build a list of places we'll look for textures
		MString searchPaths;

		// Add the standard Maya project paths
		MString workspace;
		MStatus status = MGlobal::executeCommand(MString("workspace -q -rd;"),workspace);
		if ( status == MS::kSuccess)
		{
			searchPaths += workspace;
			searchPaths += ";";
			searchPaths += workspace;
			searchPaths += "/renderData/shaders";
			MString shadersRelativePath;
			status = MGlobal::executeCommand(MString("workspace -fre shaders"),shadersRelativePath);
			if(status== MS::kSuccess)
			{
				searchPaths += ";";
				searchPaths += workspace;
				searchPaths += shadersRelativePath;
			}
		}

		if(referencePath.length() > 0)
		{
			if(searchPaths.length() > 0)
			{
				searchPaths += ";";
			}
			searchPaths += referencePath;
		}

		static char * dx11ShaderRoot = getenv("DX11SHADER_ROOT");
		if (dx11ShaderRoot)
		{
			if(searchPaths.length() > 0)
			{
				searchPaths += ";";
			}
			searchPaths += dx11ShaderRoot;
			searchPaths += ";";
			searchPaths += dx11ShaderRoot;
			searchPaths += "/shaders";
		}

		searchPaths += ";";
		searchPaths += MString("${MAYA_LOCATION}/presets/HLSL11/examples").expandEnvironmentVariablesAndTilde();

		return searchPaths;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////

	class CIncludeHelper: public ID3D10Include, public CFileReferenceHelper
	{
	public:
		STDMETHOD(Open)(D3D10_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes)
		{
			MString resolvedFileName = resolveFileName(pFileName);
			// Read the file content
			FILE* file = fopen(resolvedFileName.asChar(), "rb");
			if(file == NULL)
			{
				return E_FAIL;
			}

			// Get the file size
			fseek(file, 0, SEEK_END);
			long size = ftell(file);
			fseek(file, 0, SEEK_SET);

			// Get the file content
			char *buffer = new char[size];
			fread(buffer, 1, size, file);
			fclose(file);

			// Save the file data into ppData and the size into pBytes.
			*ppData = buffer;
			*pBytes = UINT(size);

			return S_OK;
		}
		STDMETHOD(Close)(LPCVOID pData)
		{
			char* pChar = (char*)pData;
			delete [] pChar;
			return S_OK;
		}
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	D3D10_SHADER_MACRO* getD3DMacros()
	{
		static D3D10_SHADER_MACRO macros[] = {	{ "DIRECT3D_VERSION", "0xb00" },
												{ "_MAYA_", "1"},					// similar to _3DSMAX_ and _XSI_ macros for other 3d apps
												{ "MAYA_DX11", "1"},
												{ NULL, NULL } };
		return macros;
	}

	unsigned int getShaderCompileFlags(bool useStrictness)
	{
		unsigned int flags = 0;

#ifdef _DEBUG
		// Optionally enable debugging information to be stored, without reducing performance.
		flags |= D3DCOMPILE_DEBUG;
		flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		if(useStrictness)
		{
			// Enable strictness
			flags |= D3DCOMPILE_ENABLE_STRICTNESS;
		}
		else
		{
			// Allow for backwards compatibility
			flags |= D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;
		}

		return flags;
	}

	bool effectHasHullShader(ID3DX11Effect* effect)
	{
		if(effect)
		{
			D3DX11_EFFECT_DESC effectDesc;
			effect->GetDesc(&effectDesc);
			for(unsigned int i = 0; i < effectDesc.Techniques; ++i)
			{
				ID3DX11EffectTechnique* technique = effect->GetTechniqueByIndex(i);
				if(technique && technique->IsValid())
				{
					D3DX11_TECHNIQUE_DESC techniqueDesc;
					technique->GetDesc(&techniqueDesc);
					for(unsigned int j = 0;j < techniqueDesc.Passes;++j)
					{
						ID3DX11EffectPass* pass = technique->GetPassByIndex(j); 
						if(pass && pass->IsValid())
						{
							D3DX11_PASS_SHADER_DESC shaderDesc;
							HRESULT hr = pass->GetHullShaderDesc(&shaderDesc);
							if(hr == S_OK && shaderDesc.pShaderVariable && shaderDesc.pShaderVariable->IsValid())
							{
								ID3D11HullShader* pHullShader = NULL;
								shaderDesc.pShaderVariable->GetHullShader(shaderDesc.ShaderIndex,&pHullShader);
								if(pHullShader)
								{
									//Found a hull shader
									pHullShader->Release();
									return true;
								}

							}
						}
					}
				}
			}

		}
		return false;
	}

	bool isValidEffectFile(const MString& fileName, bool& isCompiled)
	{
		MString extension;

		int idx = fileName.rindexW(L'.');
		if(idx > 0)
		{
			extension = fileName.substringW( idx+1, fileName.length()-1 );
			extension = extension.toLowerCase();
		}

		isCompiled = (extension == "fxo");
		return (extension == "fx" || extension == "fxo");
	}

	void pushError(const MString& fileName, MString &errorLog, ID3DBlob* error)
	{
		char* pMessage = (error && error->GetBufferSize() > 0) ? (char*) error->GetBufferPointer() : NULL;

		MStringArray args;
		args.append(fileName);
		args.append(MString(pMessage));

		MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorEffectCompile, args );
		errorLog += msg;
	}

	void pushError(MString &errorLog, ID3DBlob* error)
	{
		char* pMessage = (error && error->GetBufferSize() > 0) ? (char*) error->GetBufferPointer() : NULL;

		MStringArray args;
		args.append(MString(pMessage));

		MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorEffectBuffer, args );
		errorLog += msg;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////

	time_t fileTimeStamp(const MString& fileName)
	{
		struct stat statBuf;
		if( stat(fileName.asChar(), &statBuf) != 0 )
			return 0;

		return statBuf.st_mtime;
	}

	struct EffectKey
	{
		ID3D11Device* device;
		MString fileName;
		time_t timeStamp;
	};

	bool operator< (const EffectKey& lhs, const EffectKey& rhs)
	{
		return (lhs.device <  rhs.device) ||
			   (lhs.device == rhs.device && ( (lhs.timeStamp <  rhs.timeStamp) ||
											  (lhs.timeStamp == rhs.timeStamp && strcmp(lhs.fileName.asChar(), rhs.fileName.asChar()) < 0) ) );
	}

	struct MStringSorter {
		bool operator() (const MString& lhs, const MString& rhs) const
		{
			return strcmp(lhs.asChar(), rhs.asChar()) < 0;
		}
	};

	class EffectCollection
	{
	public:
		ID3DX11Effect* acquire(dx11ShaderNode* node, ID3D11Device* device, const MString& fileName);
		ID3DX11Effect* acquire(dx11ShaderNode* node, ID3D11Device* device, const MString& fileName, ID3DX11Effect* reference, ID3DX11Effect* source = NULL);
		void release(dx11ShaderNode* node, ID3DX11Effect *effect, const MString& fileName);
		void getNodesUsingEffect(const MString& fileName, ShaderNodeList &nodes) const;

		ID3DX11Effect* getReferenceEffectAndFileName(ID3DX11Effect *effect, MString& fileName) const;

	private:
		typedef std::map< EffectKey, ID3DX11Effect* > Key2ReferenceEffectMap;
		Key2ReferenceEffectMap key2ReferenceEffectMap;

		typedef std::pair< EffectKey, unsigned int > EffectKeyCountPair;
		typedef std::map< ID3DX11Effect*, EffectKeyCountPair > ReferenceCountMap;
		ReferenceCountMap referenceCountMap;

		typedef std::map< ID3DX11Effect*, ID3DX11Effect* > Clone2ReferenceMap;
		Clone2ReferenceMap clone2ReferenceMap;

		// We need to keep track of dx11ShaderNodes at all times,
		// even when compilation failed and we have no ID3DX11Effect
		// to deal with. This will allow the "Reload" button to work
		// after a shader file failed to compile.
		typedef std::set< dx11ShaderNode* > NodeSet;
		typedef std::map< MString, NodeSet, MStringSorter > Path2NodesMap;
		Path2NodesMap path2NodesMap;
	};

	//! Acquire effect from specified fileName
	//! If a reference effect is found for this fileName, return a cloned instance
	//! Update collection keeps track of :
	//!   cloned effect -> reference effect
	//!   fileName -> node
	ID3DX11Effect* EffectCollection::acquire(dx11ShaderNode* node, ID3D11Device* device, const MString& fileName)
	{
		ID3DX11Effect* effect = NULL;

		EffectKey key = { device, fileName, fileTimeStamp(fileName) } ;

		// Find reference in cache
		Key2ReferenceEffectMap::const_iterator it = key2ReferenceEffectMap.find(key);
		if(it != key2ReferenceEffectMap.end())
		{
			ID3DX11Effect* reference = it->second;
			effect = acquire(node, device, fileName, reference);
		}

		return effect;
	}

	//! Acquire effect from reference
	//! Return a cloned instance of the effect
	//! Update collection keeps track of :
	//!   cloned effect -> reference effect
	//!   fileName -> node
	ID3DX11Effect* EffectCollection::acquire(dx11ShaderNode* node, ID3D11Device* device, const MString& fileName, ID3DX11Effect* reference, ID3DX11Effect* source )
	{
		// Keep track of fileName -> node lookup, whenever the effect was loaded or not
		path2NodesMap[fileName].insert(node);

		if( reference == NULL )
			return NULL;
		if( source == NULL)
			source = reference;

		// Add the reference in cache if not in yet.
		EffectKey key = { device, fileName, fileTimeStamp(fileName) } ;
		{
			Key2ReferenceEffectMap::const_iterator it = key2ReferenceEffectMap.find(key);
			if(it == key2ReferenceEffectMap.end()) {
				key2ReferenceEffectMap.insert( std::make_pair(key, reference) );
			}
		}

		// Clone effect
		ID3DX11Effect* effect = NULL;
		HRESULT hr = source->CloneEffect(0, &effect);
		if( FAILED( hr ) || effect == NULL )
			return NULL;

		// Increase the number of clone for this reference
		// Equivalent to the number of time this effect is used
		{
			ReferenceCountMap::iterator it = referenceCountMap.find(reference);
			if(it == referenceCountMap.end())
			{
				// Not there yet, set count to 1 and register key
				referenceCountMap.insert( std::make_pair(reference, std::make_pair(key, 1) ) );
			}
			else
			{
				// Already there add 1
				++(it->second.second);
			}
		}

		// Keep track of clone -> reference lookup
		clone2ReferenceMap.insert( std::make_pair(effect, reference) );

		return effect;
	}

	//! Release the effect from cache,
	//! and release the reference if this effect was it last clone
	void EffectCollection::release(dx11ShaderNode* node, ID3DX11Effect *effect, const MString& fileName)
	{
		if (effect)
		{
			Clone2ReferenceMap::iterator it = clone2ReferenceMap.find(effect);
			if(it != clone2ReferenceMap.end())
			{
				ID3DX11Effect* reference = it->second;

				ReferenceCountMap::iterator it2 = referenceCountMap.find(reference);
				if(it2 != referenceCountMap.end())
				{
					// This was the last clone for this reference, we can release it
					if(it2->second.second == 1)
					{
						EffectKey &key = it2->second.first;
						key2ReferenceEffectMap.erase(key);
						referenceCountMap.erase(it2);
						reference->Release();
					}
					else
					{
						--(it2->second.second);
					}
				}

				clone2ReferenceMap.erase(it);
			}

			effect->Release();
		}

		// Remove this node from the fileName -> nodes lookup
		path2NodesMap[fileName].erase(node);
		// No more not for this fileName, clear it
		if (path2NodesMap[fileName].empty())
			path2NodesMap.erase(fileName);
	}

	//! Return the effect used as reference and the effect file name
	ID3DX11Effect* EffectCollection::getReferenceEffectAndFileName(ID3DX11Effect *effect, MString& fileName) const
	{
		ID3DX11Effect* reference = NULL;

		Clone2ReferenceMap::const_iterator it = clone2ReferenceMap.find(effect);
		if(it != clone2ReferenceMap.end())
		{
			reference = it->second;

			Key2ReferenceEffectMap::const_iterator it2 = key2ReferenceEffectMap.begin();
			Key2ReferenceEffectMap::const_iterator it2End = key2ReferenceEffectMap.end();
			for(; it2 != it2End; ++it2)
			{
				if(it2->second == reference)
				{
					fileName = it2->first.fileName;
					break;
				}
			}
		}

		return reference;
	}

	void EffectCollection::getNodesUsingEffect(const MString& fileName, ShaderNodeList &nodes) const
	{
		Path2NodesMap::const_iterator itNodeSet = path2NodesMap.find(fileName);
		if (itNodeSet != path2NodesMap.end())
		{
			const NodeSet& nodeSet = itNodeSet->second;
			for (NodeSet::const_iterator itNode = nodeSet.begin(); itNode != nodeSet.end(); ++itNode)
			{
				nodes.push_back(*itNode);
			}
		}
	}

	static EffectCollection gEffectCollection;

	class CompiledEffectCache {
		// Very basic LRU cache for effect files:
	public:
		CompiledEffectCache();
		~CompiledEffectCache();
		static CompiledEffectCache* get();
		ID3DX11Effect* find( ID3D11Device* device, const MString& fileName );
		void add(ID3D11Device* device, const MString& fileName, ID3DX11Effect* effect );
	private:
		struct CacheData {
			CacheData(ID3D11Device* device, const MString& fileName, ID3DX11Effect* effect, int firstAccess);
			~CacheData();
			ID3D11Device* mDevice;
			MString mFileName;
			time_t mTimeStamp;
			ID3DX11Effect* mEffect;
			int mLastAccess;
		private:
			CacheData(const CacheData&);
			const CacheData& operator=(const CacheData&);
		};
		std::list<CacheData*> mCached;
		int mAccessClock;
		static const size_t kCacheSize = 8;
		MCallbackId mExitCallback;
		MCallbackId mFileNewCallback;
	    static void flushCache( void *data);
		static CompiledEffectCache* sCachePtr;
	};

	CompiledEffectCache::CacheData::CacheData(ID3D11Device* device, const MString& fileName, ID3DX11Effect* effect, int firstAccess)
		: mDevice(device)
		, mFileName(fileName)
		, mEffect(NULL)
		, mLastAccess(firstAccess)
	{
		if (effect)
			effect->CloneEffect(0, &mEffect);
		mTimeStamp = fileTimeStamp(fileName);
	}

	CompiledEffectCache::CacheData::~CacheData()
	{
		if (mEffect)
			mEffect->Release();
	}

	CompiledEffectCache::CompiledEffectCache() : mAccessClock(0) {
	    mExitCallback = MSceneMessage::addCallback(MSceneMessage::kMayaExiting, CompiledEffectCache::flushCache );
	    mFileNewCallback = MSceneMessage::addCallback(MSceneMessage::kBeforeNew, CompiledEffectCache::flushCache );
	}

	CompiledEffectCache::~CompiledEffectCache()
	{
		std::list<CacheData*>::iterator itCache = mCached.begin();
		for ( ; itCache != mCached.end(); ++itCache )
		{
			delete *itCache;
		}
	    MSceneMessage::removeCallback( mExitCallback );
	    MSceneMessage::removeCallback( mFileNewCallback );
	}

	void CompiledEffectCache::flushCache( void *data)
	{
		delete sCachePtr;
		sCachePtr = NULL;
	}

	CompiledEffectCache* CompiledEffectCache::get()
	{
		if (!sCachePtr)
			sCachePtr = new CompiledEffectCache();
		return sCachePtr;
	}

	CompiledEffectCache* CompiledEffectCache::sCachePtr = NULL;

	ID3DX11Effect* CompiledEffectCache::find( ID3D11Device* device, const MString& fileName )
	{
		ID3DX11Effect* effect = NULL;
		// For small caches, a linear search is fine.
		std::list<CacheData*>::iterator itCache = mCached.begin();
		for ( ; itCache != mCached.end(); ++itCache )
		{
			CompiledEffectCache::CacheData *cacheItem(*itCache);
			if ( cacheItem->mDevice == device &&
				 cacheItem->mFileName == fileName &&
				 cacheItem->mTimeStamp == fileTimeStamp(fileName) ) 
			{
				cacheItem->mLastAccess = ++mAccessClock;
				cacheItem->mEffect->CloneEffect(0, &effect);
				break;
			}
		}

		return effect;
	}

	void CompiledEffectCache::add(ID3D11Device* device, const MString& fileName, ID3DX11Effect* effect )
	{
		if (mCached.size() > kCacheSize)
		{
			std::list<CacheData*>::iterator itCache = mCached.begin();
			std::list<CacheData*>::iterator oldestItem = itCache;
			itCache++;
			for ( ; itCache != mCached.end(); ++itCache )
			{
				if ( (*itCache)->mLastAccess < (*oldestItem)->mLastAccess )
					oldestItem = itCache;
			}
			CacheData* oldData(*oldestItem);
			mCached.erase(oldestItem);
			delete oldData;
		}
		CacheData* newData(new CacheData(device, fileName, effect, ++mAccessClock));
		if (newData->mEffect)
			mCached.push_back( newData );
		else
			delete newData;
	}
}

/*
	Remove effect from collection and also remove reference if it was the last effect corresponding to file path.
*/
void CDX11EffectCompileHelper::releaseEffect(dx11ShaderNode* node, ID3DX11Effect* effect, const MString& fileName)
{
	MString resolvedFileName = CDX11EffectCompileHelper::resolveShaderFileName(fileName);

	gEffectCollection.release(node, effect, resolvedFileName);
}

/*
	Get the absolute file path
*/
MString CDX11EffectCompileHelper::resolveShaderFileName(const MString& fileName, bool* fileExists)
{
	MString resolvedFileName = fileName;

	// If the fileName is absolute, no resolve needed, we keep the original full path
	if( MFileObject::isAbsolutePath(fileName) == false )
	{
		CIncludeHelper includeHelper;
		resolvedFileName = includeHelper.resolveFileName(fileName.asChar());
	}

	if( fileExists != NULL )
	{
		MFileObject file;
		file.setRawFullName( resolvedFileName );
		*fileExists = file.exists();
	}

	return resolvedFileName;
}

/*
	Load and compile a text shader file.
	- The shader is first searched in the collection, if a match is found a clone is returned
	and automatically added to the collection.
	- Else it's searched in the LRU, if found it will be cloned and added to the collection.
	- Finally if no match is found, the shader file is loaded and compiled, 
	and the effect is added to the collection as reference.
*/
ID3DX11Effect* CDX11EffectCompileHelper::build(dx11ShaderNode* node, ID3D11Device* device, const MString& fileName, MString &errorLog, bool useStrictness)
{
	bool fileExits = false;
	MString resolvedFileName = CDX11EffectCompileHelper::resolveShaderFileName(fileName, &fileExits);
	if(fileExits == false)
	{
		MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorFileNotFound, resolvedFileName );
		errorLog += msg;
		return NULL;
	}

	bool compiledEffect = false;
	if( isValidEffectFile(resolvedFileName, compiledEffect) == false )
	{
		MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorInvalidEffectFile, resolvedFileName );
		errorLog += msg;
		return NULL;
	}

	// Acquire effect from collection if it was already loaded once and will return a clone
	ID3DX11Effect *effect = gEffectCollection.acquire(node, device, resolvedFileName);
	if( effect == NULL ) {

		effect = CompiledEffectCache::get()->find(device, resolvedFileName);
		if( effect == NULL ) {

			if( resolvedFileName != fileName && MFileObject::isAbsolutePath(fileName) )
			{
				MStringArray args;
				args.append(fileName);
				args.append(resolvedFileName);

				MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorAbsolutePathNotFound, args );
				errorLog += msg;
			}

			CIncludeHelper includeHelper;
			includeHelper.setReferencePath(resolvedFileName);

			ID3DBlob *shader = NULL;
			ID3DBlob *error = NULL;
			HRESULT hr = S_FALSE;

			if( compiledEffect )
			{
				FILE* file = fopen(resolvedFileName.asChar(), "rb");
				if(file)
				{
					// Get the file size
					fseek(file, 0, SEEK_END);
					long size = ftell(file);
					fseek(file, 0, SEEK_SET);

					// Get the file content
					hr = D3DCreateBlob(size, &shader);
					if( SUCCEEDED( hr ) ) 
					{
						fread(shader->GetBufferPointer(), 1, size, file);
					}
					fclose(file);
				}
			}
			else
			{
				unsigned int compileFlags = getShaderCompileFlags(useStrictness);
				D3D10_SHADER_MACRO* macros = getD3DMacros();
#if _MSC_VER < 1700
				hr = D3DX11CompileFromFile(resolvedFileName.asChar(), macros, &includeHelper, NULL, /*MSG0*/"fx_5_0", compileFlags, 0, NULL, &shader, &error, NULL);
#else
				hr = D3DCompileFromFile(resolvedFileName.asWChar(), macros, &includeHelper, NULL, /*MSG0*/"fx_5_0", compileFlags, 0, &shader, &error);
#endif
			}

			if( FAILED( hr ) || shader == NULL )
			{
				pushError(fileName, errorLog, error);
			}
			else if( shader )
			{
				hr = D3DX11CreateEffectFromMemory(shader->GetBufferPointer(), shader->GetBufferSize(), 0, device, &effect);
				if( FAILED( hr ) || effect == NULL )
				{
					pushError(fileName, errorLog, error);
				}
			}

			if( shader ) {
				shader->Release();
			}

			if( compiledEffect == false && useStrictness == false && effect != NULL && effectHasHullShader(effect) ) {
				// if the effect has a hull shader we need to recompile it
				// with strict flag otherwise it won't support the tesselation properly :
				// for example, the geometry may not be visible
				effect->Release();
				effect = CDX11EffectCompileHelper::build(node, device, fileName, errorLog, true /*useStrictness*/);

				// return now, skip the add to cache, already done in build()
				return effect;
			}

			// Effect was compiled,
			// Add it to LRU cache
			CompiledEffectCache::get()->add(device, resolvedFileName, effect);
		}  // CompiledEffectCache::get()

		// The effect was either found in the CompiledEffectCache or compiled,
		// Acquire effect from collection, will register the compiled effect as reference and will return a clone
		effect = gEffectCollection.acquire(node, device, resolvedFileName, effect);
	} // gEffectCollection.acquire()

	return effect;
}

/*
	During a duplicate, we already have an effect to use as reference.
	The source effect will be cloned, and the result added to the cache.
*/
ID3DX11Effect* CDX11EffectCompileHelper::build(dx11ShaderNode* node, ID3D11Device* device, const MString& fileName, ID3DX11Effect* effectSource, MString &errorLog)
{
	MString resolvedFileName = CDX11EffectCompileHelper::resolveShaderFileName(fileName);

	ID3DX11Effect *effect = NULL;

	// Find effectSource in collection
	// Will gives us the original reference effect for this effect and the resolved fileName.
	MString referenceResolvedFileName;
	ID3DX11Effect *reference = gEffectCollection.getReferenceEffectAndFileName(effectSource, referenceResolvedFileName);
	if(reference != NULL && resolvedFileName == referenceResolvedFileName)
	{
		// Acquire effect from collection
		effect = gEffectCollection.acquire(node, device, resolvedFileName, reference, effectSource);
	}

	return effect;
}

/*
	Load a precompiled effect.
	The effect is not stored in any cache, as the loading of a compiled effect is already fast enough.
*/
ID3DX11Effect* CDX11EffectCompileHelper::build(dx11ShaderNode* node, ID3D11Device* device, const void* buffer, unsigned int dataSize, MString &errorLog, bool useStrictness)
{
	unsigned int compileFlags = getShaderCompileFlags(useStrictness);
	D3D10_SHADER_MACRO* macros = getD3DMacros();
	CIncludeHelper includeHelper;

	ID3DX11Effect *effect = NULL;
	ID3DBlob *shader = NULL;
	ID3DBlob *error = NULL;
	HRESULT hr = S_FALSE;
#if _MSC_VER < 1700
	hr = D3DX11CompileFromMemory((char*)buffer, dataSize, NULL, macros, &includeHelper, "", "fx_5_0", compileFlags, 0, NULL, &shader, &error, NULL);
#else
	hr = D3DCompile((char*)buffer, dataSize, NULL, macros, &includeHelper, "", "fx_5_0", compileFlags, 0, &shader, &error);
#endif
	if( FAILED( hr ) || shader == NULL )
	{
		pushError(errorLog, error);
	}
	else if( shader )
	{
		hr = D3DX11CreateEffectFromMemory(shader->GetBufferPointer(), shader->GetBufferSize(), 0, device, &effect);
		if( FAILED( hr ) || effect == NULL )
		{
			pushError(errorLog, error);
		}
	}

	if( shader ) {
		shader->Release();
	}

	if( useStrictness == false && effect != NULL && effectHasHullShader(effect) ) {
		// if the effect has a hull shader we need to recompile it
		// with strict flag otherwise it won't support the tesselation properly :
		// for example, the geometry may not be visible
		effect->Release();
		effect = CDX11EffectCompileHelper::build(node, device, buffer, dataSize, errorLog, true /*useStrictness*/);
	}

	return effect;
}

/*
	Get all the nodes that use the specified file shader.
	The collection keeps track of which shader is used by which nodes.
*/
void CDX11EffectCompileHelper::getNodesUsingEffect(const MString& fileName, ShaderNodeList &nodes)
{
	MString resolvedFileName = CDX11EffectCompileHelper::resolveShaderFileName(fileName);

	gEffectCollection.getNodesUsingEffect(resolvedFileName, nodes);
}
