//-
// Copyright 2011 Autodesk, Inc.  All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk license
// agreement provided at the time of installation or download, or which
// otherwise accompanies this software in either electronic or hard copy form.
//+
#if _MSC_VER >= 1700
#pragma warning( disable: 4005 )
#endif


#include "dx11Shader.h"
#include "dx11ShaderStrings.h"
#include "dx11ShaderCompileHelper.h"
#include "dx11ShaderUniformParamBuilder.h"
#include "dx11ConeAngleToHotspotConverter.h"
#include "crackFreePrimitiveGenerator.h"
#include "dx11ShaderSemantics.h"

#include <maya/MGlobal.h>
#include <maya/MFileIO.h>
#include <maya/MString.h>
#include <maya/MFnAmbientLight.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnStringData.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnStringArrayData.h>
#include <maya/MDGModifier.h>
#include <maya/MEventMessage.h>
#include <maya/MSceneMessage.h>
#include <maya/MPlugArray.h>
#include <maya/MFileObject.h>
#include <maya/MModelMessage.h>
#include <maya/MAngle.h>
#include <maya/MImageFileInfo.h>
#include <maya/MRenderUtil.h>
#include <maya/MAnimControl.h>

#include <maya/MVaryingParameter.h>
#include <maya/MUniformParameter.h>
#include <maya/MRenderProfile.h>
#include <maya/MGeometryList.h>
#include <maya/MPointArray.h>

#include <maya/MViewport2Renderer.h>
#include <maya/MDrawContext.h>
#include <maya/MTextureManager.h>
#include <maya/MHWGeometryUtilities.h>
#include <maya/MRenderUtilities.h>
#include <maya/MGeometryRequirements.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MUIDrawManager.h>

#include <maya/MHardwareRenderer.h>
#include <maya/MGLFunctionTable.h>
#include <maya/M3dView.h>

#include <iostream>
#include <sstream>
#include <algorithm>
#ifdef _DEBUG
	#define _DEBUG_SHADER 1
#endif
//#define PRINT_DEBUG_INFO
//#define PRINT_DEBUG_INFO_SHADOWS

#include <stdio.h>

#define M_CHECK(assertion)  if (assertion) ; else throw ((dx11Shader::InternalError*)__LINE__)
namespace dx11Shader
{
#ifdef _WIN32
	class InternalError;    // Never defined.  Used like this:
	//   throw (InternalError*)__LINE__;
#else
	struct InternalError
	{
		char* message;
	};
	//   throw (InternalError*)__LINE__;
#endif
}

namespace
{
	// Input shader attributes
	// initialized in dx11ShaderNode::initializeNodeAttrs()
	static MObject sShader;
	static MObject sTechnique;
	static MObject sTechniques;
	static MObject sDescription;
	static MObject sDiagnostics;
	static MObject sEffectUniformParameters;
	static MObject sLightInfo;

	MString MStringFromInt(int value)
	{
		MString str;
		str.set((double)value, 0);
		return str;
	}

	MString MStringFromUInt(unsigned int value)
	{
		MString str;
		str.set((double)value, 0);
		return str;
	}

	struct MUniformParameterData
	{
		MString name;
		MUniformParameter::DataType type;
		unsigned int numElements;
	};

	// Sorting Functor for std::set<MString>
	struct MStringSorter
	{
		bool operator() (const MString& lhs, const MString& rhs) const
		{
			return strcmp(lhs.asChar(), rhs.asChar()) < 0;
		}
	};
	typedef std::set<MString, MStringSorter> SetOfMString;

	// Convenient function to remove all non alpha-numeric characters from a string (remplaced by _ )
	MString sanitizeName(const MString& dirtyName)
	{
		std::string retVal(dirtyName.asChar());

		for (size_t i=0; i<retVal.size(); ++i)
			if (!isalnum(retVal[i]))
				retVal.replace(i, 1, "_");

		return MString(retVal.c_str());
	}

	MString replaceAll(const MString& str_, const MString& from_, const MString& to_)
	{
		std::string str(str_.asChar());
		std::string from(from_.asChar());
		std::string to(to_.asChar());

		std::size_t start = str.find(from);
		while(start != std::string::npos)
		{
			std::size_t len = from.size();
			std::size_t end = start + len;

			// Check if match the whole word
			if( ( start > 0 && isalnum(str.at(start-1)) ) ||
				( end < (str.size() - 1) && isalnum(str.at(end+1)) ) )
			{
				start = str.find(from, end);
				continue;
			}

			str.replace(start, len, to);
			start = str.find(from, start + to.size());
		}

		return MString(str.c_str());
	}

	// Adding and removing attributes while a scene is loading can lead
	// to issues, especially if there were connections between the shader
	// and a texture. To prevent these issues, we will wait until the scene
	// has finished loading before adding or removing the attributes that
	// manage connections between a scene light and its corresponding shader
	// parameters.
	class PostSceneUpdateAttributeRefresher
	{
	public:
		static void add(dx11ShaderNode* node)
		{
			if (sInstance == NULL)
				sInstance = new PostSceneUpdateAttributeRefresher();
			sInstance->mNodeSet.insert(node);
		};

		static void remove(dx11ShaderNode* node)
		{
			if (sInstance != NULL)
				sInstance->mNodeSet.erase(node);
		}

	private:
		PostSceneUpdateAttributeRefresher()
		{
            mSceneUpdateCallback = MSceneMessage::addCallback(MSceneMessage::kSceneUpdate, PostSceneUpdateAttributeRefresher::refresh );
            mAfterCreateReference = MSceneMessage::addCallback(MSceneMessage::kAfterCreateReference , PostSceneUpdateAttributeRefresher::refresh );
            mAfterImport = MSceneMessage::addCallback(MSceneMessage::kAfterImport, PostSceneUpdateAttributeRefresher::refresh );
            mAfterLoadReference = MSceneMessage::addCallback(MSceneMessage::kAfterLoadReference, PostSceneUpdateAttributeRefresher::refresh );
		};

		~PostSceneUpdateAttributeRefresher()
		{
            MSceneMessage::removeCallback( mSceneUpdateCallback );
            MSceneMessage::removeCallback( mAfterCreateReference );
            MSceneMessage::removeCallback( mAfterImport );
            MSceneMessage::removeCallback( mAfterLoadReference );
		}

		static void refresh(void* data)
		{
			if (sInstance)
			{
				for (TNodeSet::iterator itNode = sInstance->mNodeSet.begin();
										itNode != sInstance->mNodeSet.end();
										++itNode )
				{
					(*itNode)->refreshLightConnectionAttributes(true);
				}

				delete sInstance;
				sInstance = NULL;
			}
		}

	private:
		typedef std::set<dx11ShaderNode*> TNodeSet;
		TNodeSet mNodeSet;
        MCallbackId mSceneUpdateCallback;
        MCallbackId mAfterCreateReference;
        MCallbackId mAfterImport;
        MCallbackId mAfterLoadReference;
		static PostSceneUpdateAttributeRefresher *sInstance;
	};
	PostSceneUpdateAttributeRefresher *PostSceneUpdateAttributeRefresher::sInstance = NULL;

	class AfterOpenErrorCB
	{
	public:
		static void addError(const MString& errorMsg)
		{
			if(sInstance == NULL)
				sInstance = new AfterOpenErrorCB;

			sInstance->mErrorMsg += errorMsg;
		}

	private:
		AfterOpenErrorCB()
		{
			mSceneOpenedCallback = MSceneMessage::addCallback(MSceneMessage::kAfterOpen, AfterOpenErrorCB::afterOpen );
		}

		~AfterOpenErrorCB()
		{
			MSceneMessage::removeCallback( mSceneOpenedCallback );
		}

		static void afterOpen(void*)
		{
			if(sInstance)
			{
				MGlobal::displayError(sInstance->mErrorMsg);

				delete sInstance;
				sInstance = NULL;
			}
		}

	private:
		MCallbackId mSceneOpenedCallback;
		MString mErrorMsg;
		static AfterOpenErrorCB *sInstance;
	};
	AfterOpenErrorCB *AfterOpenErrorCB::sInstance = NULL;

	// Implicit light bindings are done without generating a dirty
	// notification that the attribute editor can catch and use to
	// update the dropdown menus and text fields used to indicate
	// the current state of the light connections. This class
	// accumulates refresh requests, and sends a single MEL command
	// to refresh the AE when the app becomes idle.
	class IdleAttributeEditorImplicitRefresher
	{
	public:
		static void activate()
		{
			if (sInstance == NULL)
				sInstance = new IdleAttributeEditorImplicitRefresher();
		};

	private:
		IdleAttributeEditorImplicitRefresher()
		{
            mIdleCallback = MEventMessage::addEventCallback( "idle", IdleAttributeEditorImplicitRefresher::refresh );
		};

		~IdleAttributeEditorImplicitRefresher()
		{
            MMessage::removeCallback( mIdleCallback );
		}

		static void refresh(void* data)
		{
			if (sInstance)
			{
				MGlobal::executeCommandOnIdle("if (exists(\"AEdx11Shader_lightConnectionUpdateAll\")) AEdx11Shader_lightConnectionUpdateAll;");
				delete sInstance;
				sInstance = NULL;
			}
		}

	private:
        MCallbackId mIdleCallback;
 		static IdleAttributeEditorImplicitRefresher *sInstance;
	};
	IdleAttributeEditorImplicitRefresher *IdleAttributeEditorImplicitRefresher::sInstance = NULL;


	// Convenient template functions to retrieve annotation from dx technique or dx resource variable
	template <typename ResourceType>
	bool getNumAnnotations(ResourceType *resource, uint32_t *numAnnotation)
	{
		// Generic version does not know how to fetch that info
		return false;
		// But there are a few specializations below:
	}

	template <>
	bool getNumAnnotations<ID3DX11EffectVariable>(ID3DX11EffectVariable *resource, uint32_t *numAnnotation)
	{
		D3DX11_EFFECT_VARIABLE_DESC varDesc;
		resource->GetDesc(&varDesc);
		*numAnnotation = varDesc.Annotations;
		return true;
	}

	template <>
	bool getNumAnnotations<ID3DX11EffectShaderResourceVariable>(ID3DX11EffectShaderResourceVariable *resource, uint32_t *numAnnotation)
	{
		return getNumAnnotations<ID3DX11EffectVariable>(resource, numAnnotation);
	}
	

	template <>
	bool getNumAnnotations<ID3DX11EffectPass>(ID3DX11EffectPass *resource, uint32_t *numAnnotation)
	{
		D3DX11_PASS_DESC varDesc;
		resource->GetDesc(&varDesc);
		*numAnnotation = varDesc.Annotations;
		return true;
	}

	template <>
	bool getNumAnnotations<ID3DX11EffectTechnique>(ID3DX11EffectTechnique *resource, uint32_t *numAnnotation)
	{
		D3DX11_TECHNIQUE_DESC varDesc;
		resource->GetDesc(&varDesc);
		*numAnnotation = varDesc.Annotations;
		return true;
	}

	template <>
	bool getNumAnnotations<ID3DX11EffectGroup>(ID3DX11EffectGroup *resource, uint32_t *numAnnotation)
	{
		D3DX11_GROUP_DESC varDesc;
		resource->GetDesc(&varDesc);
		*numAnnotation = varDesc.Annotations;
		return true;
	}

	template <typename ResourceType>
	ID3DX11EffectVariable* findAnnotationByName(ResourceType *resource, const char* annotationName)
	{
		// The latest effect 11 library is very verbose when an annotation
		// is not found by name. This version will stay quiet if the
		// annotation is not found.
		uint32_t numAnnotation = -1;
		ID3DX11EffectVariable* retVal = NULL;
		if (getNumAnnotations(resource, &numAnnotation))
		{
			for (uint32_t idx = 0; idx < numAnnotation; ++idx)
			{
				ID3DX11EffectVariable* var = resource->GetAnnotationByIndex(idx);
				if (var)
				{
					D3DX11_EFFECT_VARIABLE_DESC varDesc;
					var->GetDesc(&varDesc);

					if (strcmp(varDesc.Name, annotationName) == 0)
					{
						retVal = var;
						break;
					}
				}
			}
		}
		else
		{
			retVal = resource->GetAnnotationByName(annotationName);
		}
		return retVal;
	}

	template <typename ResourceType>
	bool getAnnotation(ResourceType *resource, const char* annotationName, MString& annotationValue)
	{
		ID3DX11EffectVariable* annotation = findAnnotationByName(resource, annotationName);
		if(annotation && annotation->IsValid())
		{
			ID3DX11EffectStringVariable* strVariable = annotation->AsString();
			if(strVariable && strVariable->IsValid())
			{
				LPCSTR value;
				if( SUCCEEDED ( strVariable->GetString( &value ) ) )
				{
					annotationValue = MString(value);
					return true;
				}
			}
		}
		return false;
	}

	template <typename ResourceType>
	bool getAnnotation(ResourceType *resource, const char* annotationName, float& annotationValue)
	{
		ID3DX11EffectVariable* annotation = findAnnotationByName(resource, annotationName);
		if(annotation && annotation->IsValid())
		{
			ID3DX11EffectScalarVariable* scalarVariable = annotation->AsScalar();
			if(scalarVariable && scalarVariable->IsValid())
			{
				float value;
				if( SUCCEEDED ( scalarVariable->GetFloat( &value ) ) )
				{
					annotationValue = value;
					return true;
				}
			}
		}
		return false;
	}

	template <typename ResourceType>
	bool getAnnotation(ResourceType *resource, const char* annotationName, int& annotationValue)
	{
		ID3DX11EffectVariable* annotation = findAnnotationByName(resource, annotationName);
		if(annotation && annotation->IsValid())
		{
			ID3DX11EffectScalarVariable* scalarVariable = annotation->AsScalar();
			if(scalarVariable && scalarVariable->IsValid())
			{
				int value;
				if( SUCCEEDED ( scalarVariable->GetInt( &value ) ) )
				{
					annotationValue = value;
					return true;
				}
			}
		}
		return false;
	}

	template <typename ResourceType>
	bool getAnnotation(ResourceType *resource, const char* annotationName, bool& annotationValue)
	{
		ID3DX11EffectVariable* annotation = findAnnotationByName(resource, annotationName);
		if(annotation && annotation->IsValid())
		{
			ID3DX11EffectScalarVariable* scalarVariable = annotation->AsScalar();
			if(scalarVariable && scalarVariable->IsValid())
			{
#if _MSC_VER < 1700
				BOOL value;
#else
				bool value;
#endif
				if( SUCCEEDED ( scalarVariable->GetBool( &value ) ) )
				{
					annotationValue = (value != 0);
					return true;
				}
			}
		}
		return false;
	}

	// Convert varying parameter semantic to geometry semantic
	MHWRender::MGeometry::Semantic getVertexBufferSemantic(MVaryingParameter::MVaryingParameterSemantic semantic)
	{
		switch (semantic) {
			case MVaryingParameter::kPosition:	return MHWRender::MGeometry::kPosition;
			case MVaryingParameter::kNormal:	return MHWRender::MGeometry::kNormal;
			case MVaryingParameter::kTexCoord:	return MHWRender::MGeometry::kTexture;
			case MVaryingParameter::kColor:		return MHWRender::MGeometry::kColor;
			case MVaryingParameter::kTangent:	return MHWRender::MGeometry::kTangent;
			case MVaryingParameter::kBinormal:	return MHWRender::MGeometry::kBitangent;
	//		case MVaryingParameter::kWeight:	return MGeometry::kInvalidSemantic;
			default: return MHWRender::MGeometry::kInvalidSemantic;
		}
	}

    static const wchar_t layerNameSeparator(L'\r');
    void getTextureDesc(const MHWRender::MDrawContext& context, const MUniformParameter& uniform, MString &fileName, MString &layerName, int &alphaChannelIdx)
    {
        if(!uniform.isATexture())
            return;
        
        fileName = uniform.getAsString(context);
        if(fileName.length() == 0)  // file name is empty no need to process the layer name
            return;

        layerName.clear();
        alphaChannelIdx = -1;

        // Find the file/layer separator .. texture name set for the uv editor .. cf dx11ShaderNode::renderImage()
        const int idx = fileName.indexW(layerNameSeparator);
        if(idx >= 0)
        {
            MStringArray splitData;
            fileName.split(layerNameSeparator, splitData);
            if(splitData.length() > 2)
                alphaChannelIdx = splitData[2].asInt();
            if(splitData.length() > 1)
                layerName = splitData[1];
            fileName = splitData[0];
        }
        else
        {
            // Look for the layerSetName attribute
            MObject node = uniform.getSource().node();
            MFnDependencyNode dependNode;
            dependNode.setObject(node);

            MPlug plug = dependNode.findPlug("layerSetName");
            if(!plug.isNull()) {
                plug.getValue(layerName);
            }

            // Look for the alpha channel index :
            // - get the select alpha channel name
            // - get the list of all alpha channels
            // - resolve index
            plug = dependNode.findPlug("alpha");
            if(!plug.isNull()) {
                MString alphaChannel;
                plug.getValue(alphaChannel);

                if(alphaChannel.length() > 0) {
                    if(alphaChannel == "Default") {
                        alphaChannelIdx = 1;
                    }
                    else {
                        plug = dependNode.findPlug("alphaList");
                        if(!plug.isNull()) {
                            MDataHandle dataHandle;
                            plug.getValue(dataHandle);
                            if(dataHandle.type() == MFnData::kStringArray) {
                                MFnStringArrayData stringArrayData (dataHandle.data());

                                MStringArray allAlphaChannels;
                                stringArrayData.copyTo(allAlphaChannels);

                                unsigned int count = allAlphaChannels.length();
                                for(unsigned int idx = 0; idx < count; ++idx) {
                                    const MString& channel = allAlphaChannels[idx];
                                    if(channel == alphaChannel) {
                                        alphaChannelIdx = idx + 2;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

	// Return only the file name (with or without extension) of a given file path
	MString getFileName(const MString &filePath, bool withExtension = false)
	{
		MFileObject file;
		file.setRawFullName(filePath);

		MString fileName = file.resolvedName();

		if(withExtension == false)
		{
			int idx = fileName.rindexW(L'.');
			if(idx > 0) fileName = fileName.substringW( 0, idx-1 );
		}

		return fileName;
	}

	// Return true if the file name (without path or extension) is the same
	bool isSameEffect(const MString &filePath1, const MString &filePath2)
	{
		// Get only the file names
		MString fileName1 = getFileName(filePath1);
		MString fileName2 = getFileName(filePath2);

		return (fileName1 == fileName2);
	}

	// Convert Maya light type to dx11Shader light type
	dx11ShaderNode::ELightType getLightType(const MHWRender::MLightParameterInformation* lightParam)
	{
		dx11ShaderNode::ELightType type = dx11ShaderNode::eUndefinedLight;

		MString lightType = lightParam->lightType();

		// The 3rd letter of the light name is a perfect hash,
		// so let's cut on the number of string comparisons.
		if (lightType.length() > 2) {
			switch (lightType.asChar()[2])
			{
			case 'o':
				if (::strcmp(lightType.asChar(),"spotLight") == 0)
					type = dx11ShaderNode::eSpotLight;
				break;

			case 'r':
				if (::strcmp(lightType.asChar(),"directionalLight") == 0)
					// The headlamp used in the "Use default lighting" mode
					// does not have the same set of attributes as a regular
					// directional light, so we must disambiguate them
					// otherwise we might not know how to fetch shadow data
					// from the regular kind.
					if (lightParam->lightPath().isValid())
						type = dx11ShaderNode::eDirectionalLight;
					else
						type = dx11ShaderNode::eDefaultLight;
				break;

			case 'i':
				if (::strcmp(lightType.asChar(),"pointLight") == 0)
					type = dx11ShaderNode::ePointLight;
				break;

			case 'b':
				if (::strcmp(lightType.asChar(),"ambientLight") == 0)
					type = dx11ShaderNode::eAmbientLight;
				break;

			case 'l':
				if (::strcmp(lightType.asChar(),"volumeLight") == 0)
					type = dx11ShaderNode::eVolumeLight;
				break;

			case 'e':
				if (::strcmp(lightType.asChar(),"areaLight") == 0)
					type = dx11ShaderNode::eAreaLight;
				break;
			}
		}
		return type;
	}

	// Determine if scene light is compatible with shader light
	bool isLightAcceptable(dx11ShaderNode::ELightType shaderLightType, dx11ShaderNode::ELightType sceneLightType)
	{
		// a Spot light is acceptable for any light types, providing both the direction and position properties.
		if(sceneLightType == dx11ShaderNode::eSpotLight)
			return true;

		// a Directional light only provides direction property.
		if(sceneLightType == dx11ShaderNode::eDirectionalLight || sceneLightType == dx11ShaderNode::eDefaultLight)
			return (shaderLightType == dx11ShaderNode::eDirectionalLight || shaderLightType == dx11ShaderNode::eAmbientLight);

		// a Point light only provides position property, same for volume and area lights
		if(sceneLightType == dx11ShaderNode::ePointLight ||
		   sceneLightType == dx11ShaderNode::eAreaLight ||
		   sceneLightType == dx11ShaderNode::eVolumeLight)
			return (shaderLightType == dx11ShaderNode::ePointLight || shaderLightType == dx11ShaderNode::eAmbientLight);

		// an Ambient light provides neither direction nor position properties.
		if(sceneLightType == dx11ShaderNode::eAmbientLight)
			return (shaderLightType == dx11ShaderNode::eAmbientLight);

		return false;
	}

	// The light information in the draw context has M attributes that we
	// want to match to the N attributes of the shader. In order to do so
	// in less than O(MxN) we create this static mapping between a light
	// semantic and the corresponding DC light attribute names whose value
	// needs to be fetched to refresh a shader parameter value.
	typedef std::vector<MStringArray> TNamesForSemantic;
	typedef std::vector<TNamesForSemantic> TSemanticNamesForLight;
	static TSemanticNamesForLight sSemanticNamesForLight(dx11ShaderNode::eLightCount);

	void buildDrawContextParameterNames(dx11ShaderNode::ELightType lightType, const MHWRender::MLightParameterInformation* lightParam)
	{
		TNamesForSemantic& namesForLight(sSemanticNamesForLight[lightType]);
		namesForLight.resize(CUniformParameterBuilder::eLastParameterType);

		MStringArray params;
		lightParam->parameterList(params);
		for (unsigned int p = 0; p < params.length(); ++p)
		{
			MString pname = params[p];
			MHWRender::MLightParameterInformation::StockParameterSemantic semantic = lightParam->parameterSemantic( pname );

			switch (semantic)
			{
			case MHWRender::MLightParameterInformation::kWorldPosition:
				namesForLight[CUniformParameterBuilder::eLightPosition].append(pname);
				if (pname == "LP0")
					namesForLight[CUniformParameterBuilder::eLightAreaPosition0].append(pname);
				if (pname == "LP1")
					namesForLight[CUniformParameterBuilder::eLightAreaPosition1].append(pname);
				if (pname == "LP2")
					namesForLight[CUniformParameterBuilder::eLightAreaPosition2].append(pname);
				if (pname == "LP3")
					namesForLight[CUniformParameterBuilder::eLightAreaPosition3].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kWorldDirection:
				namesForLight[CUniformParameterBuilder::eLightDirection].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kIntensity:
				namesForLight[CUniformParameterBuilder::eLightIntensity].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kColor:
				namesForLight[CUniformParameterBuilder::eLightColor].append(pname);
				namesForLight[CUniformParameterBuilder::eLightAmbientColor].append(pname);
				namesForLight[CUniformParameterBuilder::eLightSpecularColor].append(pname);
				namesForLight[CUniformParameterBuilder::eLightDiffuseColor].append(pname);
				break;
			// Parameter type extraction for shadow maps
			case MHWRender::MLightParameterInformation::kGlobalShadowOn:
			case MHWRender::MLightParameterInformation::kShadowOn:
				namesForLight[CUniformParameterBuilder::eLightShadowOn].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kShadowViewProj:
				namesForLight[CUniformParameterBuilder::eLightShadowViewProj].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kShadowMap:
				namesForLight[CUniformParameterBuilder::eLightShadowOn].append(pname);
				namesForLight[CUniformParameterBuilder::eLightShadowMap].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kShadowColor:
				namesForLight[CUniformParameterBuilder::eLightShadowColor].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kShadowBias:
				namesForLight[CUniformParameterBuilder::eLightShadowMapBias].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kCosConeAngle:
				namesForLight[CUniformParameterBuilder::eLightHotspot].append(pname);
				namesForLight[CUniformParameterBuilder::eLightFalloff].append(pname);
				break;
			case MHWRender::MLightParameterInformation::kDecayRate:
				namesForLight[CUniformParameterBuilder::eDecayRate].append(pname);
				break;
			default:
				break;
			}
		}
	}

	const MStringArray& drawContextParameterNames(dx11ShaderNode::ELightType lightType, int paramType, const MHWRender::MLightParameterInformation* lightParam)
	{
		if (sSemanticNamesForLight[lightType].size() == 0)
			buildDrawContextParameterNames(lightType, lightParam);

		return sSemanticNamesForLight[lightType][paramType];
	}

	/*
		Get the valid primitive topology.
		If no hull shader, return simple topology based on primitive type (points, lines, triangles ...)
		If a hull shader is detected return the according patchlist based on the primitive type and primitive patch mode
	*/
	D3D11_PRIMITIVE_TOPOLOGY getPrimitiveTopology(MHWRender::MGeometry::Primitive primitiveType, int primitiveStride, bool containsHullShader)
	{
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

		switch (primitiveType)
		{
		case MHWRender::MGeometry::kPoints:
			topology = (containsHullShader ? D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
			break;

		case MHWRender::MGeometry::kLines:
			topology = (containsHullShader ? D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
			break;

		case MHWRender::MGeometry::kLineStrip:
			topology = (containsHullShader ? D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
			break;

		case MHWRender::MGeometry::kTriangles:
			topology = (containsHullShader ? D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			break;

		case MHWRender::MGeometry::kTriangleStrip:
			topology = (containsHullShader ? D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			break;

		case MHWRender::MGeometry::kPatch:
			if(primitiveStride >= 1 && primitiveStride <= 32)
				topology = (D3D11_PRIMITIVE_TOPOLOGY)((int)(D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST) + primitiveStride - 1);
			break;

		default:
			break;
		};

		return topology;
	}

	struct MatchingParameter
	{
		MHWRender::MGeometry::Semantic semantic;
		int semanticIndex;
		int dimension;
		int elementSize;
	};
	typedef std::vector<MatchingParameter> MatchingParameters;

	void getDstSemanticsFromSrcVertexDescriptor( const MVaryingParameterList& varyingParameters, MHWRender::MVertexBufferDescriptor const &vertexDescription, MatchingParameters& parameters)
	{
		MHWRender::MGeometry::Semantic	vbSemantics = vertexDescription.semantic();

		for (int varId=0; varId < varyingParameters.length(); ++varId)
		{
			MVaryingParameter				varying				= varyingParameters.getElement(varId);
			MHWRender::MGeometry::Semantic	varyingSemantics	= getVertexBufferSemantic( varying.getSourceType() );

			if (vbSemantics==varyingSemantics) {
				MString descriptionName		= vertexDescription.name();
				MString varyingName			= varying.getSourceSetName();
				const char*		varyingMap			= strstr(varyingName.asChar(),"map");
				const char*		descriptionMap		= strstr(descriptionName.asChar(),"map");
				int	varyingBufferIndex = 0;

				bool bMatchFound = false;

				if ( varyingName.length() > 0 ) {
					bMatchFound = (descriptionName==varyingName);
					if (bMatchFound) {
						// The srcName can be overwritten, so we cannot retrieve
						// varyingBufferIndex from there, but the destName still
						// contains the information we need:
						MString varyingDestName	= varying.destinationSet();
						const char* firstDigit = varyingDestName.asChar();
						while (*firstDigit && !isdigit(*firstDigit))
							++firstDigit;

						// Maya's 'colorSet' default naming scheme differs slightly from the 'map' default naming scheme:
						bool isColorSet = (varyingSemantics == MHWRender::MGeometry::kColor);
						if (!isColorSet)
							varyingBufferIndex = *firstDigit ? atoi(firstDigit)-1 : 0;
						else
							varyingBufferIndex = *firstDigit ? atoi(firstDigit) : 0;
					}
				}
				else
				{
					varyingBufferIndex  = varyingMap ? atoi(varyingMap+3)-1 : 0;
					int	descriptionIndex	= descriptionMap ? atoi(descriptionMap+3)-1 : 0;
					bMatchFound = (descriptionIndex==varyingBufferIndex);
				}

				// we may have found the right Match let's match the source set
				// and make sure we count the occurences. for example we may have mapped both
				// normal and position on the position buffer so, the first time we will encounter
				// position it will refer to normal the second to position
				if (bMatchFound) {

					MatchingParameter param = { getVertexBufferSemantic( varying.semantic() ), varyingBufferIndex, varying.dimension(), varying.getElementSize() };
					parameters.push_back(param);
				}
			}
		}

	}

	struct dx11SemanticInfo
	{
		const char*										Name;
		MVaryingParameter::MVaryingParameterSemantic	Type;
		int												MinElements;
		int												MaxElements;
	};

	/*
		Convert an effect variable to a varying parameter.

		Standard semantics are Position, Normal, TexCoord, Tangent, Binormal and Color,
		any other value will be considered as a custom semantic and given the textCoord type.
		A custom semantic should be associated with a vertex buffer generator to provide the desired content.

		Two hard-coded custom semantics are handled by the plugin and intended to work with
		the customPrimitiveGenerator plugin example.
		When the customPositionStream and customNormalStream semantics are identified, they are given
		the position and normal types respectively, and the custom primitive name customPrimitiveTest.
		The customPrimitiveGenerator plugin implements two vertex buffer generators and a primitive generator.
*/

	void appendVaryingParameter(const D3D11_SIGNATURE_PARAMETER_DESC &paramDesc, MVaryingParameterList& varyingParameters, MString &errorLog, MString &warningLog, MString &customIndexBufferType)
	{
		static const dx11SemanticInfo gDx11SemanticInfo[] =
		{
			{ dx11ShaderSemantic::kPosition,	MVaryingParameter::kPosition,	3, 4 } ,
			{ dx11ShaderSemantic::kNormal,		MVaryingParameter::kNormal,		3, 4 } ,
			{ dx11ShaderSemantic::kTexCoord,	MVaryingParameter::kTexCoord,	2, 2 } ,
			{ dx11ShaderSemantic::kTangent,		MVaryingParameter::kTangent,	3, 4 } ,
			{ dx11ShaderSemantic::kBinormal,	MVaryingParameter::kBinormal,	3, 4 } ,
			{ dx11ShaderSemantic::kColor,		MVaryingParameter::kColor,		4, 4 }
		};
		static int dx11SemanticInfoCount = sizeof(gDx11SemanticInfo) / sizeof(dx11SemanticInfo);
		static int dx11SemanticTextCoordId = 2;

		int semanticInfoId = 0;
		for (; semanticInfoId < dx11SemanticInfoCount; ++semanticInfoId) {
			if (::_stricmp(paramDesc.SemanticName, gDx11SemanticInfo[semanticInfoId].Name ) == 0) break;
		}

		bool isCustomSemantic = false;
		if(semanticInfoId >= dx11SemanticInfoCount)
		{
			// This is a custom named input.
			isCustomSemantic = true;

			static bool enableCustomPrimitiveGenerator = (getenv("MAYA_USE_CUSTOMPRIMITIVEGENERATOR") != NULL);

			if(enableCustomPrimitiveGenerator && ::_stricmp(paramDesc.SemanticName, dx11ShaderSemanticValue::kCustomPositionStream) == 0) {
				semanticInfoId = 0; // Position
				customIndexBufferType = dx11ShaderSemanticValue::kCustomPrimitiveTest;
			}
			else if(enableCustomPrimitiveGenerator && ::_stricmp(paramDesc.SemanticName, dx11ShaderSemanticValue::kCustomNormalStream) == 0) {
				semanticInfoId = 1; // Normal
				customIndexBufferType = dx11ShaderSemanticValue::kCustomPrimitiveTest;
			}
			else {
				// Treat it as a texture but retain the semantic name.
				semanticInfoId = dx11SemanticTextCoordId;
			}
		}

		MVaryingParameter::MVaryingParameterSemantic fieldType = gDx11SemanticInfo[semanticInfoId].Type;
		int minWidth = gDx11SemanticInfo[semanticInfoId].MinElements;
		int maxWidth = gDx11SemanticInfo[semanticInfoId].MaxElements;

		const char* semanticName = isCustomSemantic ? paramDesc.SemanticName : gDx11SemanticInfo[semanticInfoId].Name;

		int fieldWidth = 0;
		switch (paramDesc.Mask)
		{
		case 15:	fieldWidth = 4; break;
		case 7:		fieldWidth = 3; break;
		case 3:		fieldWidth = 2; break;
		case 1:		fieldWidth = 1; break;
		default:	fieldWidth = 0; break;
		}

		MVaryingParameter::MVaryingParameterType dataType = MVaryingParameter::kInvalidParameter;
		switch (paramDesc.ComponentType)
		{
		case D3D10_REGISTER_COMPONENT_FLOAT32:	dataType = MVaryingParameter::kFloat; break;
		case D3D10_REGISTER_COMPONENT_SINT32:	dataType = MVaryingParameter::kInt32; break;
		case D3D10_REGISTER_COMPONENT_UINT32:	dataType = MVaryingParameter::kUnsignedInt32; break;
		default:								dataType = MVaryingParameter::kInvalidParameter; break;
		}

		std::ostringstream destinationSet;
		std::ostringstream name;

		switch (fieldType)
		{
		case MVaryingParameter::kTexCoord:
		case MVaryingParameter::kTangent:
		case MVaryingParameter::kBinormal:
			{
				destinationSet << "map" << (paramDesc.SemanticIndex+1);
				name << semanticName << int(paramDesc.SemanticIndex);
			}
			break;
		case MVaryingParameter::kColor:
			{
				destinationSet << "colorSet";
				if ( paramDesc.SemanticIndex > 0 )
					destinationSet << paramDesc.SemanticIndex;

				name << semanticName << int(paramDesc.SemanticIndex);
				break;
			}
		default:
			{
				name << semanticName;
				if (paramDesc.SemanticIndex > 0)
				{
					MStringArray args;
					args.append( semanticName );

					MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorIndexVaryingParameter, args );
					errorLog += msg;
				}
			}
			break;
		}

		MVaryingParameter varying(
			MString(name.str().c_str()),
			dataType,
			minWidth,
			maxWidth,
			fieldWidth,
			fieldType,
			MString(destinationSet.str().c_str()),
			false,
			(isCustomSemantic ? MString(semanticName) : MString())
			);

		varyingParameters.append(varying);

		if (!isCustomSemantic)
		{
			// We have a few cases of 3rd party effects that declare a float4 TexCoord
			// try to tell the user why the shader will not render correctly:
			if ((int(fieldWidth)<gDx11SemanticInfo[semanticInfoId].MinElements) || (int(fieldWidth)>gDx11SemanticInfo[semanticInfoId].MaxElements) ) {
				MStringArray args;
				args.append( paramDesc.SemanticName );
				args.append( MStringFromUInt(paramDesc.SemanticIndex) );
				args.append( MStringFromInt(gDx11SemanticInfo[semanticInfoId].MinElements) );
				args.append( MStringFromInt(gDx11SemanticInfo[semanticInfoId].MaxElements) );
				args.append( MStringFromInt(fieldWidth) );

				MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorVertexRequirement, args );
				errorLog += msg;
			} /*else if ( int(fieldWidth)!=gDx11SemanticInfo[semanticInfoId].MayaSize) {
				MStringArray args;
				args.append( semanticName );
				args.append( MStringFromInt(gDx11SemanticInfo[semanticInfoId].MayaSize) );
				args.append( MStringFromInt(fieldWidth) );

				MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kWarningVertexRequirement, args );
				warningLog += msg;
			}
			*/
		}
	}

	/*
		Convenient function to create the list of varying parameter for a specified technique.
		This is used when loading an effect to the dx11shader and also when creating a temporary
		effect for the swatch and uv editor render.
	*/
	void buildVaryingParameterList(dx11ShaderDX11EffectTechnique* dxTechnique, unsigned int numPasses, MVaryingParameterList& varyingParameters, MString &errorLog, MString &warningLog, MString &customIndexBufferType)
	{
		SetOfMString registeredSemantics;
		for (unsigned int passId = 0; passId < numPasses; ++passId)
		{
			dx11ShaderDX11Pass* dxPass = dxTechnique->GetPassByIndex(passId);
			if(dxPass == NULL || dxPass->IsValid() == false)
				continue;

			D3DX11_PASS_SHADER_DESC vertexShaderDesc;
			dxPass->GetVertexShaderDesc(&vertexShaderDesc);

			ID3DX11EffectShaderVariable* shaderVar = vertexShaderDesc.pShaderVariable;
			if(shaderVar == NULL)
				continue;

			unsigned int shaderIndex = vertexShaderDesc.ShaderIndex;
			D3DX11_EFFECT_SHADER_DESC shaderDesc;
			shaderVar->GetShaderDesc(shaderIndex, &shaderDesc);
			for (unsigned int varId = 0; varId < shaderDesc.NumInputSignatureEntries; ++varId)
			{
				D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
				shaderVar->GetInputSignatureElementDesc(shaderIndex, varId, &paramDesc);

				// Build a unique name based on semantic name + semantic index
				MString uniqueName;
				uniqueName.set( (double)paramDesc.SemanticIndex, 0 );
				uniqueName = MString(paramDesc.SemanticName) + MString("_") + uniqueName;

				if( registeredSemantics.count(uniqueName) == 0 )
				{
					appendVaryingParameter(paramDesc, varyingParameters, errorLog, warningLog, customIndexBufferType);
					registeredSemantics.insert(uniqueName);
				}
			}
		}
	}

	/*
		Create a temporary effect and build the associated varying and uniform parameters list
		Used for the swatch and uv editor render.
	*/
	bool buildTemporaryEffect(dx11ShaderNode *shaderNode, dx11ShaderDX11Device *dxDevice, const char* bufferData, unsigned int bufferSize,
					dx11ShaderDX11Effect*& dxEffect, dx11ShaderDX11EffectTechnique*& dxTechnique, unsigned int& numPasses,
					MVaryingParameterList*& varyingParameters, MUniformParameterList*& uniformParameters, MString &customIndexBufferType)
	{
		MString errorLog;
		dxEffect = CDX11EffectCompileHelper::build(shaderNode, dxDevice, bufferData, bufferSize, errorLog);
		if(dxEffect == NULL)
			return false;

		// Only use first technique
		dxTechnique = dxEffect->GetTechniqueByIndex(0);
		if(dxTechnique != NULL && dxTechnique->IsValid())
		{
			D3DX11_TECHNIQUE_DESC desc;
			dxTechnique->GetDesc(&desc);
			numPasses = desc.Passes;
		}

		// Invalid effect
		if(numPasses == 0)
		{
			CDX11EffectCompileHelper::releaseEffect(shaderNode, dxEffect, "TemporaryEffect");
			dxEffect = NULL;
			dxTechnique = NULL;
			return false;
		}

		// Create a new uniform parameters list and fill it from the effect
		{
			uniformParameters = new MUniformParameterList;

			D3DX11_EFFECT_DESC effectDesc;
			dxEffect->GetDesc(&effectDesc);
			for (unsigned int varId = 0; varId < effectDesc.GlobalVariables; ++varId)
			{
				ID3DX11EffectVariable* dxVar = dxEffect->GetVariableByIndex(varId);
				CUniformParameterBuilder builder;
				builder.init(dxVar, shaderNode, varId);
				if(builder.build())
					uniformParameters->append(builder.getParameter());
			}
		}

		// Create a new varying parameters list and fill it from the passes
		{
			varyingParameters = new MVaryingParameterList;

			MString warningLog;
			buildVaryingParameterList(dxTechnique, numPasses, *varyingParameters, errorLog, warningLog, customIndexBufferType);
		}

		return true;
	}

	/*
		Acquire a reference geometry with geometry requirements extracted from the varying parameters list
	*/
	MHWRender::MGeometry* acquireReferenceGeometry( MHWRender::MGeometryUtilities::GeometricShape shape, const MVaryingParameterList& varyingParameters)
	{
		MHWRender::MGeometryRequirements requirements;
		{
			typedef std::map<MHWRender::MGeometry::Semantic, SetOfMString> RegisteredVaryingParameters;
			RegisteredVaryingParameters registeredVaryingParameters;

			for(int paramId = 0; paramId < varyingParameters.length(); ++paramId)
			{
				MVaryingParameter varying = varyingParameters.getElement(paramId);

				MHWRender::MGeometry::Semantic sourceSemantic = getVertexBufferSemantic(varying.getSourceType());
				MString	sourceSetName = varying.getSourceSetName();

				if(registeredVaryingParameters[sourceSemantic].count(sourceSetName) > 0)
					continue;
				registeredVaryingParameters[sourceSemantic].insert(sourceSetName);

				MHWRender::MVertexBufferDescriptor desc(
					sourceSetName,
					sourceSemantic,
					(MHWRender::MGeometry::DataType) varying.type(),
					varying.dimension() );
				desc.setSemanticName(varying.semanticName());

				requirements.addVertexRequirement( desc );
			}
			{
				MHWRender::MIndexBufferDescriptor desc(
					MHWRender::MIndexBufferDescriptor::kTriangle,
					MString(),
					MHWRender::MGeometry::kTriangles );
				requirements.addIndexingRequirement( desc );
			}
		}
		return MHWRender::MGeometryUtilities::acquireReferenceGeometry( shape, requirements );
	}

#ifdef USE_GL_TEXTURE_CACHING
	unsigned int createGLTextureFromTarget(MHWRender::MRenderTarget* textureTarget, float &scaleU, float &scaleV)
	{
		MGLFunctionTable *_GLFT = MHardwareRenderer::theRenderer()->glFunctionTable();
		if(_GLFT == NULL)
			return 0;

		if(textureTarget == NULL)
			return 0;

		MHWRender::MRenderTargetDescription targetDesc;
		textureTarget->targetDescription(targetDesc);

		if(targetDesc.rasterFormat() != MHWRender::kR8G8B8A8_UNORM)
			return 0;

		int targetRowPitch, targetSlicePitch;
		unsigned char* targetData = (unsigned char*)(textureTarget->rawData(targetRowPitch, targetSlicePitch));
		if(targetData == NULL)
			return 0;

		unsigned int targetWidth = targetDesc.width();
		unsigned int targetHeight = targetDesc.height();

		MGLsizei textureWidth = targetWidth;
		MGLsizei textureHeight = targetHeight;
		scaleU = scaleV = 1.0f;

		int bytesPerPixel = 4;	// the target is MHWRender::kR8G8B8A8_UNORM
		int realTargetRowPitch = bytesPerPixel * targetWidth;

		if(_GLFT->extensionExists(kMGLext_ARB_texture_non_power_of_two) == false || realTargetRowPitch != targetRowPitch)
		{
			int pow2Width = 1;
			while(pow2Width < textureWidth)
				pow2Width <<= 1;

			int pow2Height = 1;
			while(pow2Height < textureHeight)
				pow2Height <<= 1;

			// If the device doesn't support non-powered of 2 texture size, we need to rearrange the image.
			// And we also need to rearrange image when row pitch is different from the 'real/calculated' row pitch
			if(pow2Width != textureWidth || pow2Height != textureHeight || realTargetRowPitch != targetRowPitch )
			{
				int pow2RowPitch = bytesPerPixel * pow2Width;

				unsigned int pow2DataSize = pow2RowPitch * pow2Height;
				unsigned char* pow2Data = new unsigned char[pow2DataSize];
				memset(pow2Data, 0, pow2DataSize);

				unsigned int targetOffset = 0;
				unsigned int pow2DataOffset = 0;

				for(unsigned int row = 0; row < targetHeight; ++row)
				{
					memcpy(pow2Data + pow2DataOffset, targetData + targetOffset, realTargetRowPitch);

					pow2DataOffset += pow2RowPitch;
					targetOffset += targetRowPitch;
				}

				scaleU = (float) textureWidth / (float) pow2Width;
				scaleV = (float) textureHeight / (float) pow2Height;
				textureWidth = pow2Width;
				textureHeight = pow2Height;

				delete [] targetData;
				targetData = pow2Data;
			}
		}

		MGLuint glTextureId = 0;
		// Create GL texture
		_GLFT->glGenTextures(1, &glTextureId);

		//_GLFT->glEnable(_kGL_TEXTURE_2D);
		_GLFT->glBindTexture(MGL_TEXTURE_2D, glTextureId);

		if((_GLFT->extensionExists(kMGLext_SGIS_generate_mipmap)))
			_GLFT->glTexParameteri(MGL_TEXTURE_2D, MGL_GENERATE_MIPMAP_SGIS, MGL_TRUE);

		// the target is MHWRender::kR8G8B8A8_UNORM
		_GLFT->glTexImage2D(MGL_TEXTURE_2D, 0, MGL_RGBA, textureWidth, textureHeight, 0, MGL_RGBA, MGL_UNSIGNED_BYTE, targetData);

		delete [] targetData;

		return glTextureId;
	}

	void releaseGLTexture(unsigned int textId)
	{
		MGLFunctionTable *_GLFT = MHardwareRenderer::theRenderer()->glFunctionTable();
		if(_GLFT == NULL)
			return;

		if(textId == 0)
			return;

		MGLuint glTextureId = textId;
		glDeleteTextures(1, &glTextureId);
	}

	bool renderGLTexture(unsigned int textId, float scaleU, float scaleV, floatRegion region, bool unfiltered)
	{
		MGLFunctionTable *_GLFT = MHardwareRenderer::theRenderer()->glFunctionTable();
		if(_GLFT == NULL)
			return false;

		if(textId == 0)
			return false;

		MGLuint glTextureId = textId;

		_GLFT->glPushAttrib(MGL_TEXTURE_BIT | MGL_COLOR_BUFFER_BIT | MGL_ENABLE_BIT);
		_GLFT->glPushClientAttrib(MGL_CLIENT_VERTEX_ARRAY_BIT);

		_GLFT->glEnable(MGL_TEXTURE_2D);
		_GLFT->glBindTexture(MGL_TEXTURE_2D, glTextureId);

		_GLFT->glTexParameteri(MGL_TEXTURE_2D, MGL_TEXTURE_WRAP_S, MGL_REPEAT);
		_GLFT->glTexParameteri(MGL_TEXTURE_2D, MGL_TEXTURE_WRAP_T, MGL_REPEAT);

		if(unfiltered)
			_GLFT->glTexParameteri(MGL_TEXTURE_2D, MGL_TEXTURE_MAG_FILTER, MGL_NEAREST);
		else
			_GLFT->glTexParameteri(MGL_TEXTURE_2D, MGL_TEXTURE_MAG_FILTER, MGL_LINEAR);

		_GLFT->glBlendFunc(MGL_SRC_ALPHA, MGL_ONE_MINUS_SRC_ALPHA);
		_GLFT->glEnable(MGL_BLEND);

		// Draw
		_GLFT->glBegin(MGL_QUADS);
		_GLFT->glTexCoord2f(region[0][0] * scaleU, region[0][1] * scaleV);
		_GLFT->glVertex2f(region[0][0], region[0][1]);
		_GLFT->glTexCoord2f(region[0][0] * scaleU, region[1][1] * scaleV);
		_GLFT->glVertex2f(region[0][0], region[1][1]);
		_GLFT->glTexCoord2f(region[1][0] * scaleU, region[1][1] * scaleV);
		_GLFT->glVertex2f(region[1][0], region[1][1]);
		_GLFT->glTexCoord2f(region[1][0] * scaleU, region[0][1] * scaleV);
		_GLFT->glVertex2f(region[1][0], region[0][1]);
		_GLFT->glEnd();

		_GLFT->glBindTexture(MGL_TEXTURE_2D, 0);

		_GLFT->glPopClientAttrib();
		_GLFT->glPopAttrib();

		return true;
	}
#endif //USE_GL_TEXTURE_CACHING

	// Always good to reuse attributes whenever possible.
	//
	// In order to fully reuse the technique enum attribute, we need to
	// clear it of its previous contents, which is something that is not
	// yet possible with the MFnEnumAttribute function set. We still can
	// achieve the required result with a proper MEL command to reset the
	// enum strings.
	bool resetTechniqueEnumAttribute(const dx11ShaderNode& shader)
	{
		MStatus stat;
		MFnDependencyNode node(shader.thisMObject(), &stat);
		if (!stat) return false;

		// Reset the .techniqueEnum attribute if exists
		MObject attr = node.attribute("techniqueEnum", &stat);
		if (stat && !attr.isNull() && attr.apiType() == MFn::kEnumAttribute)
		{
			MFnEnumAttribute enumAttr(attr);
			MString addAttrCmd = enumAttr.getAddAttrCmd();
			if (addAttrCmd.indexW(" -en ") >= 0)
			{
				MPlug techniquePlug = node.findPlug(attr, false);
				MString resetCmd = "addAttr -e -en \"\" ";
				MGlobal::executeCommand(resetCmd + techniquePlug.name(), false, false);
			}
		}

		return true;
	}

	MObject buildTechniqueEnumAttribute(const dx11ShaderNode& shader)
	{
		MStatus stat;
		MFnDependencyNode node(shader.thisMObject(), &stat);
		if (!stat) return MObject::kNullObj;

		// Reset the .techniqueEnum attribute
		resetTechniqueEnumAttribute(shader);

		// Create the new .techniqueEnum attribute
		MObject attr = node.attribute("techniqueEnum", &stat);
		if (attr.isNull())
		{
			MFnEnumAttribute enumAttr;
			attr = enumAttr.create("techniqueEnum", "te", 0, &stat);
			if (!stat || attr.isNull()) return MObject::kNullObj;

			// Set attribute flags
			enumAttr.setInternal( true );
			enumAttr.setStorable( false );
			enumAttr.setKeyable( true );  // show in Channel Box
			enumAttr.setAffectsAppearance( true );
			enumAttr.setNiceNameOverride("Technique");

			// Add the attribute to the node
			node.addAttribute(attr);
		}

		// Set attribute fields
		MFnEnumAttribute enumAttr(attr);
		const MStringArray& techniques = shader.techniques();
		M_CHECK(techniques.length() < (unsigned	int)std::numeric_limits<short>::max());
		for (unsigned int i = 0; i < techniques.length(); i++)
		{
			enumAttr.addField(techniques[i], (short)i);
		}

		return attr;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

dx11ShaderNode::LightParameterInfo::LightParameterInfo(ELightType lightType, bool hasLightTypeSemantics)
	: fLightType(lightType)
	, fHasLightTypeSemantics(hasLightTypeSemantics)
	, fIsDirty(true)
{
}

dx11ShaderNode::ELightType dx11ShaderNode::LightParameterInfo::lightType() const
{
	ELightType type = fLightType;

	// If the type is undefined - the light annotation was not helpful -
	// find out the type based of position and direction requirements
	if( type == dx11ShaderNode::eUndefinedLight )
	{
		bool requiresPosition = false;
		bool requiresSquarePosition = false;
		bool requiresDirection = false;

		LightParameterInfo::TConnectableParameters::const_iterator it    = fConnectableParameters.begin();
		LightParameterInfo::TConnectableParameters::const_iterator itEnd = fConnectableParameters.end();
		for (; it != itEnd; ++it)
		{
			const int parameterType = it->second;

			if( parameterType == CUniformParameterBuilder::eLightPosition )
				requiresPosition = true;
			if( parameterType == CUniformParameterBuilder::eLightAreaPosition0 )
				requiresSquarePosition = true;
			else if( parameterType == CUniformParameterBuilder::eLightDirection )
				requiresDirection = true;
		}

		if( requiresPosition && requiresDirection )
			type = dx11ShaderNode::eSpotLight;
		else if (requiresSquarePosition)
			type = dx11ShaderNode::eAreaLight;
		else if ( requiresPosition )
			type = dx11ShaderNode::ePointLight;
		else if ( requiresDirection )
			type = dx11ShaderNode::eDirectionalLight;
		else
			type = dx11ShaderNode::eAmbientLight;

		// Assign the type back
		(const_cast<LightParameterInfo*>(this))->fLightType = type;
	}

	return type;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//--------------------------------------------------------------------//
// Constructor:
//
dx11ShaderNode::dx11ShaderNode()
	: fGeometryVersionId(0)
	, fLastFrameStamp((MUint64)-1)
	, fDuplicateNodeSource(NULL)
	, fPostDuplicateCallBackId(NULL)
	, fEffect(NULL)
	, fTechniqueTextureMipMapLevels(1)
	, fTechniqueIndexBufferType()
	, fVaryingParametersUpdateId(0)
	, fVaryingParametersGeometryVersionId(size_t(-1))
	, fForceUpdateTexture(true)
	, fFixedTextureMipMapLevels(-1)
	, fUVEditorTexture(NULL)
#ifdef USE_GL_TEXTURE_CACHING
    , fUVEditorLastAlphaChannel(-1)
	, fUVEditorGLTextureId(0)
#endif //USE_GL_TEXTURE_CACHING
	, fBBoxExtraScalePlugName()
	, fBBoxExtraScaleValue(0.0f)
	, fMayaSwatchRenderVar(NULL)
	, fErrorCount(0)
	, fShaderChangesGeo(false)
	, fLastTime(0)
	, fVariableNameAsAttributeName(true)
	, fMayaGammaCorrectVar(NULL)
	, fImplicitAmbientLight(-1)
{
	resetData();
	fErrorLog.clear();

	static bool addedResourcePath = false;
	if (!addedResourcePath)
	{
		MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
		if (theRenderer)
		{
			MHWRender::MTextureManager* txtManager = theRenderer->getTextureManager();
			if (txtManager)
			{
				MString resourceLocation(MString("${MAYA_LOCATION}\\presets\\HLSL11\\examples").expandEnvironmentVariablesAndTilde());
				txtManager->addImagePath( resourceLocation );
			}
		}

		addedResourcePath = true;
	}
}

// Destructor:
//
dx11ShaderNode::~dx11ShaderNode()
{
	PostSceneUpdateAttributeRefresher::remove(this);
	resetData();
	fErrorLog.clear();
}

/* static */
MTypeId	dx11ShaderNode::typeId()
{
	// This typeid must be unique across the universe of Maya plug-ins.
	// The typeid is a unique 32bit indentifier that describes this node.
	// It is used to save and retrieve nodes of this type from the binary
	// file format.  If it is not unique, it will cause file IO problems.
	static MTypeId sId( 0x00081054 );
	return sId;
}

// ========== dx11ShaderNode::creator ==========
//
//	Description:
//		this method exists to give Maya a way to create new objects
//      of this type.
//
//	Return Value:
//		a new object of this type.
//
/* static */
void* dx11ShaderNode::creator()
{
	return new dx11ShaderNode();
}


// ========== dx11ShaderNode::initialize ==========
//
//	Description:
//		This method is called to create and initialize all of the attributes
//      and attribute dependencies for this node type.  This is only called
//		once when the node type is registered with Maya.
//
//	Return Values:
//		MS::kSuccess
//		MS::kFailure
//
/* static */
MStatus dx11ShaderNode::initialize()
{
	MStatus ms;

	try
	{
		initializeNodeAttrs();
	}
	catch ( ... )
	{
		MGlobal::displayError( "dx11Shader internal error: Unhandled exception in initialize" );
		ms = MS::kFailure;
	}

	return ms;
}


/*
	Create common static attributes which will always appear regardless of the shader
*/
/* static */
void dx11ShaderNode::initializeNodeAttrs()
{
	MFnTypedAttribute	typedAttr;
	MFnNumericAttribute numAttr;
	MFnStringData		stringData;
	MFnStringArrayData	stringArrayData;
	MStatus				stat, stat2;

	// The shader attribute holds the name of the .fx file that defines
	// the shader
	//
	sShader = typedAttr.create("shader", "s", MFnData::kString, stringData.create(&stat2), &stat);
	M_CHECK( stat );
	typedAttr.setInternal( true);
	typedAttr.setKeyable( false );
	typedAttr.setAffectsAppearance( true );
	typedAttr.setUsedAsFilename( true );
	stat = addAttribute(sShader);
	M_CHECK( stat );

	//
	// Effect Uniform Parameters
	//
	sEffectUniformParameters = typedAttr.create("EffectParameters", "ep", MFnData::kString, stringData.create(&stat2), &stat);
	M_CHECK( stat );
	typedAttr.setInternal( true);
	typedAttr.setKeyable( false);
	typedAttr.setAffectsAppearance( true );
	stat = addAttribute(sEffectUniformParameters);
	M_CHECK( stat );

	//
	// technique
	//
	sTechnique = typedAttr.create("technique", "t", MFnData::kString, stringData.create(&stat2), &stat);
	M_CHECK( stat );
	typedAttr.setInternal( true);
	typedAttr.setKeyable( true);
	typedAttr.setAffectsAppearance( true );
	stat = addAttribute(sTechnique);
	M_CHECK( stat );

	//
	// technique list
	//
	sTechniques = typedAttr.create("techniques", "ts", MFnData::kStringArray, stringArrayData.create(&stat2), &stat);
	M_CHECK( stat );
	typedAttr.setInternal( true);
	typedAttr.setKeyable( false);
	typedAttr.setStorable( false);
	typedAttr.setWritable( false);
	typedAttr.setAffectsAppearance( true );
	stat = addAttribute(sTechniques);
	M_CHECK( stat );

	// The description field where we pass compile errors etc back for the user to see
	//
	sDescription = typedAttr.create("description", "desc", MFnData::kString, stringData.create(&stat2), &stat);
	M_CHECK( stat );
	typedAttr.setKeyable( false);
	typedAttr.setWritable( false);
	typedAttr.setStorable( false);
	stat = addAttribute(sDescription);
	M_CHECK( stat );

	// The feedback field where we pass compile errors etc back for the user to see
	//
	sDiagnostics = typedAttr.create("diagnostics", "diag", MFnData::kString, stringData.create(&stat2), &stat);
	M_CHECK( stat );
	typedAttr.setKeyable( false);
	typedAttr.setWritable( false);
	typedAttr.setStorable( false);
	stat = addAttribute(sDiagnostics);
	M_CHECK( stat );

	// The description field where we pass compile errors etc back for the user to see
	//
	sLightInfo = typedAttr.create("lightInfo", "linfo", MFnData::kString, stringData.create(&stat2), &stat);
	M_CHECK( stat );
	typedAttr.setKeyable( false);
	typedAttr.setWritable( false);
	typedAttr.setStorable( false);
	stat = addAttribute(sLightInfo);
	M_CHECK( stat );

	//
	// Specify our dependencies
	//
	attributeAffects( sShader, sTechniques);
	attributeAffects( sShader, sTechnique);
}

// Query the renderers supported by this shader
//
const MRenderProfile& dx11ShaderNode::profile()
{
	static MRenderProfile sProfile;
	if(sProfile.numberOfRenderers() == 0)
		sProfile.addRenderer(MRenderProfile::kMayaOpenGL);

	return sProfile;
}

/*
	Handle when a node is duplicated or copied.

	Store the minimum data required (the shader file path, the current technique name and id)
	and install a callback on when the duplicate operation is completed.

	The callback is mandatory because copyInternalData() is called too soon during the duplicate process:
	the node is not yet added to the dependency graph,
	but we need it to be when updating the uniform parameters list (initialization of plugs).
*/
void dx11ShaderNode::copyInternalData( MPxNode* pSrc )
{
	const dx11ShaderNode & src = *(dx11ShaderNode*)pSrc;
	fEffectName = src.fEffectName;
	fTechniqueIdx = src.fTechniqueIdx;
	fTechniqueName = src.fTechniqueName;

	// Only setup the call back if a valid effect is loaded
	if(src.fEffect != NULL && src.fEffect->IsValid())
	{
		fDuplicateNodeSource = (dx11ShaderNode*)pSrc;

		// Install callback to initialize the node once added to the dependency graph.
		fPostDuplicateCallBackId = MModelMessage::addAfterDuplicateCallback( dx11ShaderNode::postDuplicateCB, this, NULL );
	}

	MPxHardwareShader::copyInternalData(pSrc);
}
/*
	Initialize the node after it has been duplicated and added to the dependency graph.
*/
/*static*/
void dx11ShaderNode::postDuplicateCB( void* data )
{
	dx11ShaderNode* shader = (dx11ShaderNode*)data;

	// De install the callback
	MMessage::removeCallback( shader->fPostDuplicateCallBackId );
	shader->fPostDuplicateCallBackId = NULL;

	// The duplicate command comes in 3 flavors :
	// - Duplicate Without Network
	//		No nodes is duplicated (no textures will be linked to the shader)
	// - Duplicate Shading Network
	//		Nodes (textures) are duplicated and linked to the new shader
	//		ie. the original shader is linked to texture node file1, the duplicated shader will be linked to a new texture node file2.
	// - Duplicate With Connections to Network
	//		Nodes are linked as is to the new shader
	//		ie. the orginal shader is linked to texture node file1, the duplicated shader will be linked to the same texture node file1.
	// In the last two modes, the plugs and attributes will be created and connected internally.

	// The dx11Shader uses a uniform parameters list to create its own attributes based on the list of parameters of the shader file.
	// To mimic the same behaviour that occurs when duplicating internal shader nodes (blinn, lambert ..) for the different duplicate modes,
	// we perform the following actions :
	// - Keep track of the connections done internally on the duplicated node (see dx11ShaderNode::connectionMade())
	// - Don't connect any texture parameter (see dx11ShaderNode::getTextureFile())
	// - From the connections we build a mapping of which attributes was linked to which uniform parameter
	//   and eventually break the connection that we won't used (STEP ONE below).
	// - Finally, once the uniform parameters list is built for the new duplicated shader, we link the duplicated attributes to the proper
	//   uniform parameters (STEP TWO below).

	// ------------------
	// STEP ONE
	// Plugs and attributes are driven by the uniformParameters list.
	// Having plug duplicated will create unused connection
	// -> Break all duplicated connections
	std::vector< std::pair< MPlug, MUniformParameterData > > attributesToConnect;
	std::vector< std::pair< MPlug, unsigned int > > lightAttributesToConnect;
	unsigned int plugCount = shader->fDuplicatedConnections.length();
	if(plugCount > 0)
	{
		const MUniformParameterList& uniformParameters = shader->fDuplicateNodeSource->fUniformParameters;
		unsigned int nUniform = uniformParameters.length();

		const LightParameterInfoVec& lightParameters = shader->fDuplicateNodeSource->fLightParameters;
		unsigned int nLight = (unsigned int)lightParameters.size();

		MDGModifier dgModifier;

		for(unsigned int i = 0; i < plugCount; )
		{
			const MPlug& plug = shader->fDuplicatedConnections[i++];
			const MPlug& dstPlug = shader->fDuplicatedConnections[i++];

			// Find the uniform parameter this duplicated plug comes from
			MFnAttribute dstPlugAttr(dstPlug.attribute());
			MString dstPlugAttrName = dstPlugAttr.name();

			bool isConnectedLightAttr = ( dstPlugAttrName.indexW("_connected_light") > -1 );
			if(isConnectedLightAttr)
			{
				for( unsigned int l = 0; l < nLight; ++l )
				{
					const LightParameterInfo& lightInfo = lightParameters[l];
					MFnAttribute connectedLightAttr(lightInfo.fAttrConnectedLight);
					MString connectedLightAttrName = connectedLightAttr.name();
					if(connectedLightAttrName == dstPlugAttrName)
					{
						lightAttributesToConnect.push_back( std::make_pair( plug, l ) );
						break;
					}
				}
			}
			else
			{
				for( unsigned int u = 0; u < nUniform; ++u )
				{
					MUniformParameter elem = uniformParameters.getElement(u);
					MPlug elemPlug = elem.getPlug();
					if(elemPlug.isNull() == false)
					{
						MFnAttribute elemPlugAttr(elemPlug.attribute());
						MString elemPlugAttrName = elemPlugAttr.name();
						if(elemPlugAttrName == dstPlugAttrName)
						{
							MUniformParameterData param = { elem.name(), elem.type(), elem.numElements() };
							attributesToConnect.push_back( std::make_pair( plug, param ) );
							printf("  uniform <<%s>>\n", param.name.asChar());
							break;
						}
					}
				}
			}

			dgModifier.disconnect(plug.node(), plug.attribute(), dstPlug.node(), dstPlug.attribute());
		}

		dgModifier.doIt();
	}

	// The effect name and technique were set by copyInternalData.
	// load the effect and create the parameters.
	shader->reload();

	// ------------------
	// STEP TWO
	// The uniform parameters list is now built
	// Reconnect broken connections with corresponding uniform parameters
	unsigned int attributesToConnectCount = (unsigned int)attributesToConnect.size();
	unsigned int lightAttributesToConnectCount = (unsigned int)lightAttributesToConnect.size();
	if(attributesToConnectCount > 0 || lightAttributesToConnectCount > 0)
	{
		MDGModifier dgModifier;

		MUniformParameterList& uniformParameters = shader->fUniformParameters;
		unsigned int nUniform = uniformParameters.length();

		for(unsigned int i = 0; i < attributesToConnectCount; ++i)
		{
			const MPlug& srcPlug = attributesToConnect[i].first;
			const MUniformParameterData& duplicatedElement = attributesToConnect[i].second;

			// Find the uniform which correspond to the original duplicated element
			for( unsigned int u = 0; u < nUniform; ++u )
			{
				MUniformParameter elem = uniformParameters.getElement(u);
				if(	elem.type()			== duplicatedElement.type &&
					elem.numElements()	== duplicatedElement.numElements &&
					elem.name()			== duplicatedElement.name )
				{
					MPlug elemPlug = elem.getPlug();
					dgModifier.connect(srcPlug.node(), srcPlug.attribute(), elemPlug.node(), elemPlug.attribute());
					break;
				}
			}
		}

		LightParameterInfoVec& lightParameters = shader->fLightParameters;
		unsigned int nLight = (unsigned int)lightParameters.size();

		for(unsigned int i = 0; i < lightAttributesToConnectCount; ++i)
		{
			const MPlug& srcPlug = lightAttributesToConnect[i].first;
			unsigned int l = lightAttributesToConnect[i].second;

			if(l < nLight)
			{
				LightParameterInfo& lightInfo = lightParameters[l];
				dgModifier.connect(srcPlug.node(), srcPlug.attribute(), shader->thisMObject(), lightInfo.fAttrConnectedLight);
			}
		}


		dgModifier.doIt();
	}

	shader->fDuplicateNodeSource = NULL;
	shader->fDuplicatedConnections.clear();
}

bool dx11ShaderNode::setInternalValueInContext( const MPlug& plug, const MDataHandle& handle, MDGContext& context)
{
	bool retVal = true;

	try
	{
		if (plug == sShader)
		{
			loadEffect ( handle.asString() );
		}
		else if (plug == sTechnique)
		{
			setTechnique( handle.asString() );
		}
		else if (plug == fTechniqueEnumAttr)
		{
			int index = handle.asShort();
			M_CHECK(fTechniqueNames.length() < (unsigned int)std::numeric_limits<int>::max());
			if (index >= 0 && index < (int)fTechniqueNames.length() && index != fTechniqueIdx)
			{
				setTechnique(index);
			}
		}
		else
		{
			if (fBBoxExtraScalePlugName.length() > 0)
			{
				const MString plugName = plug.name();
				if (plugName == fBBoxExtraScalePlugName)
				{
					fBBoxExtraScaleValue = handle.asFloat();
				}
			}

			// Cannot check the varying input update id at this point
			// as they are not yet modified.
			retVal = MPxHardwareShader::setInternalValueInContext(plug, handle, context);
		}
	}
	catch( ... )
	{
		reportInternalError( __FILE__, __LINE__ );
		retVal = false;
	}

	return retVal;
}

/* virtual */
bool dx11ShaderNode::getInternalValueInContext( const MPlug& plug, MDataHandle& handle, MDGContext& context)
{
	bool retVal = true;

	try
	{
		if (plug == sShader)
		{
			handle.set( fEffectName );
		}
		else if (plug == sTechnique)
		{
			const MString tname = activeTechniqueName();
			handle.set( tname );
		}
		else if (plug == fTechniqueEnumAttr)
		{
			if (fTechniqueIdx >= 0)
			{
				handle.set((short)fTechniqueIdx);
			}
		}
		else if (plug == sTechniques)
		{
			const MStringArray* tlist = &techniques();
			if (tlist)
				handle.set( MFnStringArrayData().create( *tlist ));
			else
				handle.set( MFnStringArrayData().create() );
		}
		else
		{
			retVal = MPxHardwareShader::getInternalValueInContext(plug, handle, context);
		}
	}
	catch ( ... )
	{
		reportInternalError( __FILE__, __LINE__ );
		retVal = false;
	}

	return retVal;
}

/* virtual */
MStatus dx11ShaderNode::connectionMade( const MPlug& plug, const MPlug& otherPlug, bool asSrc )
{
	if( fDuplicateNodeSource != NULL && asSrc == false)
	{
		// push source plug first
		fDuplicatedConnections.append(otherPlug);
		fDuplicatedConnections.append(plug);
	}

	return MS::kUnknownParameter;
}

// Set the dirty flag on a specific shader light when the user changes
// the light connection settings in order to refresh the shader light
// bindings at the next redraw.
MStatus dx11ShaderNode::setDependentsDirty(const MPlug & plugBeingDirtied, MPlugArray & affectedPlugs)
{
	for(size_t shaderLightIndex = 0; shaderLightIndex < fLightParameters.size(); ++shaderLightIndex )
	{
		LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];

		MPlug implicitLightPlug(thisMObject(), shaderLightInfo.fAttrUseImplicit);
		if ( implicitLightPlug == plugBeingDirtied )
			shaderLightInfo.fIsDirty = true;

		MPlug connectedLightPlug(thisMObject(), shaderLightInfo.fAttrConnectedLight);
		if ( connectedLightPlug == plugBeingDirtied )
			shaderLightInfo.fIsDirty = true;
	}

	return MPxHardwareShader::setDependentsDirty(plugBeingDirtied, affectedPlugs);
}

// ***********************************
// Topology Management
// ***********************************

bool dx11ShaderNode::hasUpdatedVaryingInput() const
{
	// Test if Varying parameters have changed
	// increate DataVersionId so that lists are rebuilt
	// ---------------------------------------------
	unsigned int varyingUpdateId = 0;
	for (int i=0; i < fVaryingParameters.length(); ++i)
	{
		MVaryingParameter varying = fVaryingParameters.getElement(i);
		varyingUpdateId += varying.getUpdateId();
	}

	if (fVaryingParametersUpdateId != varyingUpdateId)
	{
		dx11ShaderNode* nonConstThis = const_cast<dx11ShaderNode*>(this);
		nonConstThis->fVaryingParametersUpdateId = varyingUpdateId;
		nonConstThis->setTopoDirty();
		return true;
	}
	return false;
}

void dx11ShaderNode::setTopoDirty()
{
	// Will be interpreted as a topo change at next redraw
	// via dx11ShaderOverride::rebuildAlways
	++fGeometryVersionId;
}

// ***********************************
// Effect Management
// ***********************************

/*
	Reload all the nodes that shared the same specified effect
*/
bool dx11ShaderNode::reloadAll(const MString& effectName)
{
	CDX11EffectCompileHelper::ShaderNodeList nodes;
	CDX11EffectCompileHelper::getNodesUsingEffect(effectName, nodes);
	CDX11EffectCompileHelper::ShaderNodeList::const_iterator it = nodes.begin();
	CDX11EffectCompileHelper::ShaderNodeList::const_iterator itEnd = nodes.end();
	if (it != itEnd)
	{
		for(; it != itEnd; ++it)
		{
			dx11ShaderNode* node = *it;
			node->reload();
		}
	}
	else
	{
		// Shaders that failed to load will not be registered
		// in the compiler helper. Iterate the scene to find them.
		MItDependencyNodes it(MFn::kPluginHardwareShader);

		while(!it.isDone())
		{
			MFnDependencyNode fn(it.item());

			if( fn.typeId() == typeId() ) {
				dx11ShaderNode* shaderNode = (dx11ShaderNode*)(fn.userNode());
				if(shaderNode && shaderNode->effectName() == effectName)
				{
					shaderNode->reload();
				}
			}
			it.next();
		}
	}

	return true;
}

/*
	Reload the effect from the current file
*/
bool dx11ShaderNode::reload()
{
	int currTechnique = fTechniqueIdx; //activeTechnique();

	loadEffect(fEffectName);

	if(currTechnique >= 0 && currTechnique < techniqueCount())
	{
		setTechnique(currTechnique);
	}

	// Allow implicit rebinding:
	for (size_t i = 0; i < fLightParameters.size(); ++i) {
		fLightParameters[i].fCachedImplicitLight = MObject();
		setLightParameterLocking(fLightParameters[i], false);
	}
	// Refresh any AE that monitors implicit lights:
	IdleAttributeEditorImplicitRefresher::activate();

	return true;
}

//
// Set the shader
//
bool dx11ShaderNode::loadEffect( const MString& shader)
{
	if (shader.length() == 0)
	{
		clearParameters();
		resetData(true);
		setUniformParameters( fUniformParameters, true );
		setVaryingParameters( fVaryingParameters, true );
		resetTechniqueEnumAttribute(*this);
		fEffectName = shader;
		return true;
	}

	bool loadedEffect = false;

	bool fileExits = true;
	if (MFileIO::isReadingFile() || MFileIO::isOpeningFile()) {
		MString resolvedFileName = CDX11EffectCompileHelper::resolveShaderFileName(shader, &fileExits);
		if(!fileExits)
		{
			MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorFileNotFound, resolvedFileName );
			fErrorLog += msg;
			displayErrorAndWarnings();
		}
	}

	MHWRender::MRenderer *theRenderer = MHWRender::MRenderer::theRenderer();
	if (theRenderer && theRenderer->drawAPIIsOpenGL() == false && fileExits)
	{
		ID3D11Device* dxDevice = (ID3D11Device*)theRenderer->GPUDeviceHandle();
		if (dxDevice)
		{
			//Clear errors and warning which might be linked to another effect
			MPlug diagnosticsPlug( thisMObject(), sDiagnostics);
			diagnosticsPlug.setValue(MString(""));

			{
				// do an early check to see if this file exists to avoid attributes getting reset in loadFromFile() if user
				// makes a typo in file path.
				bool fileExits = false;
				MString resolvedFileName = CDX11EffectCompileHelper::resolveShaderFileName(shader, &fileExits);
				if(fileExits == false)
				{
					MString msg = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorFileNotFound, resolvedFileName );
					fErrorLog += msg;
					displayErrorAndWarnings();
					return false;	// exit early so we do not store 'shader' as fEffectName in case the user saves the file at this point we don't want to save invalid fx filename
				}

				// Do not clear the effect. This allows reusing attributes whenever
				// possible. MPxHardwareShader will take care of updating metadata
				// like default values and slider limits when re-using an attribute.

				loadedEffect = loadFromFile( shader, dxDevice );

				// increment version id
				if (loadedEffect)
				{
					fEffectName = CDX11EffectCompileHelper::resolveShaderFileName(shader);
					if (techniqueCount() > 0) {
						// default to first technique:
						MPlug techniquePlug( thisMObject(), sTechnique);
						techniquePlug.setValue( techniques()[0] );
					}
				}

				// Update our shader info attributes

				MPlug descriptionPlug( thisMObject(), sDescription);
				descriptionPlug.setValue( "" );
			}

			if(loadedEffect) {
				setTopoDirty();
			}
		}
	}

	if(loadedEffect == false)
	{
		// Always keep the effect name, especially in OpenGL mode.
		fEffectName = shader;
	}

	return loadedEffect;
}

/*
	Load an effect from file.
*/
bool dx11ShaderNode::loadFromFile( const MString &fileName, dx11ShaderDX11Device* dxDevice )
{
	if (!dxDevice)
		return false;

	resetData();

	fEffect = CDX11EffectCompileHelper::build(this, dxDevice, fileName, fErrorLog);
	if( fEffect == NULL )
	{
		displayErrorAndWarnings();
		return false;
	}

	// Try to initialize the effect
	//
    if (!initializeEffect())
    {
		displayErrorAndWarnings();
		resetData();
        return false;
    }

	// Keep track of file name as effect name. We
	// don't store the full filename since that contains the
	// full path resolved using shader search paths.
	// It is assumed that on every load we must re-resolve.
	fEffectName = fileName;
	return true;
}

/*
	Load an effect from a system memory buffer.
	A unique identifier should be supplied
*/
bool dx11ShaderNode::loadFromBuffer( const MString &identifier, const void *pData, unsigned int dataSize, dx11ShaderDX11Device* dxDevice )
{
	if (!dxDevice || !pData || dataSize == 0)
		return false;

	resetData();

	fEffect = CDX11EffectCompileHelper::build(this, dxDevice, pData, dataSize, fErrorLog);
	if( fEffect == NULL )
	{
		displayErrorAndWarnings();
		return false;
	}

	// Try to initialize the effect
	//
    if (!initializeEffect())
    {
		displayErrorAndWarnings();
		resetData();
		return false;
    }

	// Keep track of identifier as effect name
	fEffectName = identifier;
	return true;
}

/*
	Parse the effect to pull of information
*/
bool dx11ShaderNode::initializeEffect()
{
	// Clear out all other data but not the effect
	//
	resetData( false );

	if (!fEffect)
		return false;

    // Initialize the list of techniques.  If there are no valid techniques, the D3D effect cannot
    // be used and the function returns false.
    initializeTechniques();
    if (techniqueCount() == 0)
    {
		displayErrorAndWarnings();
        return false;
    }
	return true;
}

/*
	Reset data members. Optionally delete
	any existing effect.
*/
void dx11ShaderNode::resetData(bool clearEffect)
{
	fMayaSwatchRenderVar = NULL;
	fMayaGammaCorrectVar = NULL;
	fTechnique = NULL;
	if (clearEffect && fEffect)
	{
		CDX11EffectCompileHelper::releaseEffect(this, fEffect, fEffectName);
		fEffect = NULL;

		// Release textures
		releaseAllTextures();
	}

	fTechniqueIdx = -1;
	fTechniqueName.clear();
	fTechniqueNames.clear();
	fPassCount = 0;

	clearLightConnectionData();

	// Should Clear the uniform parameters
	// ------------------------------------------
	fUniformParameters.setLength(0);

	// Should Clear the varying parameters
	// ------------------------------------------
	fVaryingParametersUpdateId = 0;
	fVaryingParameters.setLength(0);
	fVaryingParametersVertexDescriptorList.clear();

	fTechniqueIndexBufferType.clear();
	fTechniqueTextureMipMapLevels = 1;
	fTechniqueIsTransparent = eOpaque;
	fOpacityPlugName = "";
	fTransparencyTestProcName = "";
	fTechniqueSupportsAdvancedTransparency = false;
	fTechniqueOverridesDrawState = false;
	fForceUpdateTexture = true;
	fFixedTextureMipMapLevels = -1;
	releaseTexture(fUVEditorTexture);
	fUVEditorTexture = NULL;

#ifdef USE_GL_TEXTURE_CACHING
	fUVEditorLastTexture.clear();
	fUVEditorLastLayer.clear();
    fUVEditorLastAlphaChannel = -1;
	fUVEditorBaseColor[0] = fUVEditorBaseColor[1] = fUVEditorBaseColor[2] = fUVEditorBaseColor[3] = 0;
	fUVEditorShowAlphaMask = false;
	releaseGLTexture(fUVEditorGLTextureId);
	fUVEditorGLTextureId = 0;
	fUVEditorGLTextureScaleU = fUVEditorGLTextureScaleV = 1.0f;
#endif //USE_GL_TEXTURE_CACHING

	// clear has hull shader map cache
	fPassHasHullShaderMap.clear();

	// clear and release input layout cache
	{
		PassInputLayoutMap::iterator it = fPassInputLayoutMap.begin();
		PassInputLayoutMap::iterator itEnd = fPassInputLayoutMap.end();
		for(; it != itEnd; ++it)
		{
			InputLayoutData& data = it->second;
			data.inputLayout->Release();
			delete [] data.layoutDesc;
		}
		fPassInputLayoutMap.clear();
	}
}

// ***********************************
// Techniques Management
// ***********************************

/*
	Effects can now use multi-pass drawing to achieve interesting
	looks by specifying for each DX11EffectPass if it should be
	rendered in the current context. This is done by matching the
	context string with an annotation named "drawContext" on the
	effect pass. For example, an effect with displacement can provide
	a special shadow pass to cast correct shadows on nearby geometries.
*/
bool dx11ShaderNode::techniqueHandlesContext(const MString& requestedContext) const
{
	bool handlesContext = false;

	if(fEffect && fEffect->IsValid() && fTechnique && fTechnique->IsValid())
	{
		D3DX11_TECHNIQUE_DESC desc;
		fTechnique->GetDesc(&desc);
		for (unsigned int iPass = 0; iPass < desc.Passes && !handlesContext; ++iPass)
		{
			ID3DX11EffectPass *dxPass = fTechnique->GetPassByIndex(iPass);
			if(dxPass && dxPass->IsValid())
			{
				MString drawContext;
				getAnnotation(dxPass, "drawContext", drawContext);
				if (::_stricmp(drawContext.asChar(), requestedContext.asChar()) == 0)
				{
					handlesContext = true;
				}
			}
		}
	}
	return handlesContext;
}

/*
	Parse the effect to find all of the techniques.
	If none found then return false.
*/
bool dx11ShaderNode::initializeTechniques()
{
	fTechnique = NULL;
	fTechniqueIdx = -1;
	fTechniqueName.clear();
	fTechniqueNames.clear();

	resetTechniqueEnumAttribute(*this);

	if (!fEffect)
		return false;

	// Get the description of the D3D effect.
    D3DX11_EFFECT_DESC descEffect;
    HRESULT hr = fEffect->GetDesc(&descEffect);
	if (FAILED(hr) || descEffect.Techniques == 0)
	{
		fErrorLog += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorNoValidTechniques );
		displayErrorAndWarnings();
		return false;
	}

    // Search the D3D effect for valid techniques, adding each one to the technique list.
    for (unsigned int i = 0; i < descEffect.Techniques; ++i)
    {
        // Check whether the technique with the current index is valid.
        ID3DX11EffectTechnique* dxTechnique = fEffect->GetTechniqueByIndex(i);
        if (!dxTechnique->IsValid()) continue;

        // Add the technique's name to the technique list.
        D3DX11_TECHNIQUE_DESC desc;
        dxTechnique->GetDesc(&desc);
        fTechniqueNames.append( desc.Name );
    }

	// Set up techniqueEnum attribute. It will show up in ChannelBox
	fTechniqueEnumAttr = buildTechniqueEnumAttribute(*this);

    // Set the first technique, if any.
    if (techniqueCount() == 0)
	{
		fErrorLog += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorNoValidTechniques );
		displayErrorAndWarnings();
		return false;
	}

	return true;
}

bool dx11ShaderNode::setTechnique( const MString & techniqueName )
{
	if (!fEffect && fEffectName.length() > 0)
	{
		// Keep the name, we are in OpenGL and could not compile the effect,
		// or in DX but could not find or compile the shader file.
		fTechniqueName = techniqueName;
		// We have no idea if other techniques are available, but
		// still show something in the techniques dropdown in the
		// attribute editor. We know at least this one exists.
		fTechniqueNames.append( techniqueName );
		fTechniqueEnumAttr = buildTechniqueEnumAttribute(*this);
		return false;
	}

	if (!fEffect)
		return false;

	// MStringArray has no find method. Do a linear search...
	int numTechniques = techniqueCount();
	for (int i=0; i<numTechniques; ++i) {
		if (fTechniqueNames[i] == techniqueName) {
			 return setTechnique(i);
		}
	}
	fErrorLog += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorSetTechniqueByName, techniqueName );
	displayErrorAndWarnings();
	return false;
}

/*
	Set the current active technique by number.
*/
bool dx11ShaderNode::setTechnique( int techniqueNumber )
{
	setTopoDirty();

	if (!fEffect)
		return false;

	// Invalid technique number return failure
	if (techniqueNumber < 0 || techniqueNumber >= techniqueCount())
	{
		MString techniqueNumberStr = MStringFromInt(techniqueNumber);
		fErrorLog += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorSetTechniqueByIndex, techniqueNumberStr );
		displayErrorAndWarnings();
		return false;
	}

	// Do nothing if the technique is already active.
    if (fTechniqueIdx == techniqueNumber)
		return true;

    // Get the technique by name, and record it as the active technique.
    fTechnique = fEffect->GetTechniqueByName( fTechniqueNames[techniqueNumber].asChar() );
    if (!fTechnique)
	{
		MString techniqueNumberStr = MStringFromInt(techniqueNumber);
		fErrorLog += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorSetTechniqueByIndex, techniqueNumberStr );
		displayErrorAndWarnings();
		return false;
	}

	// Keep track of the active technique and technique number
    fTechniqueIdx = techniqueNumber;
	fTechniqueName = fTechniqueNames[fTechniqueIdx];

    // Record the number of passes for the active technique.
	// Set no active pass.
    D3DX11_TECHNIQUE_DESC desc;
    fTechnique->GetDesc(&desc);
    fPassCount = desc.Passes;

	// Light names are affected by the chosen technique:
	// -------------------------------------------------
	clearLightConnectionData();

	// must now update and create the attribute of the node
	// relative to the shader effect specific parameters
	// ----------------------------------------------------
	buildUniformParameterList();
	setUniformParameters( fUniformParameters );

	initMayaParameters();

	// Update the varying parameters
	// update the required buffers from
	// -------------------------------------------------------
	storeDefaultTextureNames();

	buildVaryingParameterList();
	setVaryingParameters(fVaryingParameters);

	initTechniqueParameters();

	restoreDefaultTextureNames();

	return true;
}

/*
	Store the default textures for the UV editor to
	preserve them when the user switches techniques
*/
void dx11ShaderNode::storeDefaultTextureNames()
{
	fDefaultTextureNames.clear();
	fDefaultTextureNames.setLength( fVaryingParameters.length() );

	MFnDependencyNode depFn( thisMObject() );
	for (int iVar = 0; iVar < fVaryingParameters.length(); ++iVar)
	{
		MVaryingParameter elem = fVaryingParameters.getElement(iVar);
		if( elem.getSourceType() != MVaryingParameter::kTexCoord )
			continue;

		MString attrName( elem.name() );
		attrName += "_DefaultTexture";
		MPlug defaultTexPlug = depFn.findPlug( attrName );

		if( defaultTexPlug.isNull() )
			continue;

		MString texName;
		defaultTexPlug.getValue( texName );
		fDefaultTextureNames[iVar] = texName;
	}
}

/*
	Restore the default textures for the UV editor after
	user has switched techniques
*/
void dx11ShaderNode::restoreDefaultTextureNames()
{
	MFnDependencyNode depFn( thisMObject() );
	for (int iVar = 0; iVar < fVaryingParameters.length(); ++iVar)
	{
		if (iVar >= (int)fDefaultTextureNames.length())
			return;

		MVaryingParameter elem = fVaryingParameters.getElement(iVar);
		if( elem.getSourceType() != MVaryingParameter::kTexCoord )
			continue;

		// checking for no string here makes sure that in initTechniqueParameters() the
		// "UVEditorOrder" semantic is respected when the user loads a new shader, because
		// we default to an empty string in the texture data group in AE.
		if (fDefaultTextureNames[iVar].length() == 0)
			continue;

		MString attrName( elem.name() );
		attrName += "_DefaultTexture";
		MPlug defaultTexPlug = depFn.findPlug( attrName );

		if( defaultTexPlug.isNull() )
			continue;

		defaultTexPlug.setValue( fDefaultTextureNames[iVar] );
	}
}

/*
	Initialize any parameter that will change the behaviour of the new selected technique:

	kIndexBufferType - defines the name of the generator that can produce the proper geometry indexing for this technique.
	kTextureMipmaplevels - controls the mipmap levels of the textures loaded/used by this technique
	kOverridesDrawState/kIsTransparent - affect how the material will be rendered.
*/
void dx11ShaderNode::initTechniqueParameters()
{
	// Query technique for index buffer type (ie. crackFree indexing - PNAEN9 or PNAEN18)
	MString newIndexBufferType;
	getAnnotation(fTechnique, dx11ShaderAnnotation::kIndexBufferType, newIndexBufferType);
	if(fTechniqueIndexBufferType != newIndexBufferType)
	{
		fTechniqueIndexBufferType = newIndexBufferType;
		setTopoDirty();
	}

	// Query technique for mipmap levels to use for when loading textures
	int mipmapLevels = 1;
	getAnnotation(fTechnique, dx11ShaderAnnotation::kTextureMipmaplevels, mipmapLevels);
	if(fTechniqueTextureMipMapLevels != mipmapLevels)
	{
		fTechniqueTextureMipMapLevels = mipmapLevels;
		fForceUpdateTexture = true;
	}

	// Query technique if it should follow the maya transparent object rendering or is self-managed (multi-passes)
	fTechniqueOverridesDrawState = false;
	getAnnotation(fTechnique, dx11ShaderAnnotation::kOverridesDrawState, fTechniqueOverridesDrawState);

	// Query technique if it supports advanced transparency algorithm.
	fTechniqueSupportsAdvancedTransparency = false;
	getAnnotation(fTechnique, dx11ShaderAnnotation::kSupportsAdvancedTransparency, fTechniqueSupportsAdvancedTransparency);

	// Query technique if it has transparency
	fTechniqueIsTransparent = eOpaque;
	int techniqueTransparentAnnotation = 0;
	if( getAnnotation(fTechnique, dx11ShaderAnnotation::kIsTransparent, techniqueTransparentAnnotation) == false )
	{
		// If the annotation is not found we need to check if
		// the effect modifies the blend state when activating
		// its passes:
		ID3D11Device *device;
		if( S_OK == fEffect->GetDevice(&device) )
		{
			// Acquire a temporary context to apply the pass on
			ID3D11DeviceContext *deviceContext;
			if( S_OK == device->CreateDeferredContext(0, &deviceContext) )
			{
				for( unsigned int passId = 0; passId < fPassCount; ++passId)
				{
					ID3DX11EffectPass *dxPass = fTechnique->GetPassByIndex(passId);
					if( dxPass == NULL || dxPass->IsValid() == false )
						continue;

					if( S_OK == dxPass->Apply(0, deviceContext) == false )
						continue;

					ID3D11BlendState *blendState;
					FLOAT blendFactor[4];
					UINT sampleMask;
					deviceContext->OMGetBlendState(&blendState, blendFactor, &sampleMask);
					if(blendState)
					{
						D3D11_BLEND_DESC blendDesc;
						blendState->GetDesc(&blendDesc);

						// Check first renderTarget only
						if( blendDesc.RenderTarget[0].BlendEnable != FALSE )
						{
							if( blendDesc.RenderTarget[0].SrcBlend  > D3D11_BLEND_ONE ||
								blendDesc.RenderTarget[0].DestBlend > D3D11_BLEND_ONE )
							{
								fTechniqueIsTransparent = eTransparent;
								break;
							}
						}
					}
				}

				// Release temporary context
				deviceContext->Release();
			}
			device->Release();
		}
	}
	else
	{
		/*  This annotation helps Maya find out if the effect is currently
			transparent or not. There are 4 possible cases:

			eOpaque: Effect is fully opaque
			eTransparent: Effect is not perfectly opaque
			eScriptedTest: The opacity of the effect can be found by executing
			               the script provided in the transparencyTest annotation
			eTestOpacitySemantics: The opacity of the effect is driven by a single
			                       float parameter that has the "opacity" semantics
		*/
		switch (techniqueTransparentAnnotation)
		{
		case eOpaque:
			fTechniqueIsTransparent = eOpaque;
			break;
		case eTransparent:
			fTechniqueIsTransparent = eTransparent;
			break;
		case eScriptedTest:
			fTechniqueIsTransparent = eScriptedTest;
			{
				// Need to find script info
				MString procBody;
				if (getAnnotation(fTechnique, dx11ShaderAnnotation::kTransparencyTest, procBody) &&
					procBody.length() > 0)
				{
#ifdef _DEBUG_SHADER
					printf("-- transparencyTest <<%s>>.\n", procBody.asChar());
#endif
					//
					unsigned int paramCount = fUniformParameters.length();
					for( unsigned int i = 0; i < paramCount; ++i )
					{
						MUniformParameter param = fUniformParameters.getElement(i);
						ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)param.userData();
						if(effectVariable)
						{
							D3DX11_EFFECT_VARIABLE_DESC varDesc;
							effectVariable->GetDesc(&varDesc);

							const MString varName(varDesc.Name);

							MFnAttribute attr(param.getPlug().attribute());
							const MString attrName = attr.name();

							static const MString queryFormat ( "`getAttr($shader + \".^1s\")`" );
							MString query;
							query.format( queryFormat, attrName );

							procBody = replaceAll(procBody, varName, query);
						}
					}

					static const MString procNameFormat ( "dx11Shader^1sTransparencyTest" );
					fTransparencyTestProcName = getFileName(fEffectName);
					fTransparencyTestProcName = sanitizeName(fTransparencyTestProcName);
					fTransparencyTestProcName.format( procNameFormat, fTransparencyTestProcName );

					static const MString procBodyFormat ( "global proc int ^1s(string $shader) { return (^2s); }" );
					procBody.format( procBodyFormat, fTransparencyTestProcName, procBody );
#ifdef _DEBUG_SHADER
					printf("-- transparencyTest <<%s>>.\n", procBody.asChar());
#endif
					// eval the body only once to define the procedure
					if (MGlobal::executeCommand(procBody, false, false) == MS::kSuccess)
					{
						break;
					}
				}

				// NOTE: on failure to get procedure we clear values and fall back to auto (ie. no break here)
				fTransparencyTestProcName = "";
			}
		case eTestOpacitySemantics:
			fTechniqueIsTransparent = eTestOpacitySemantics;
			{
				// Need to find opacity plug:
				bool foundOpacity = false;
				unsigned int paramCount = fUniformParameters.length();
				for( unsigned int i = 0; i < paramCount && !foundOpacity; ++i )
				{
					MUniformParameter param = fUniformParameters.getElement(i);

					// look for opacity -- filter for float1 parameters
					if( param.type() == MUniformParameter::kTypeFloat && param.numElements() == 1)
					{
						ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)param.userData();
						if(effectVariable)
						{
							D3DX11_EFFECT_VARIABLE_DESC varDesc;
							effectVariable->GetDesc(&varDesc);

							// Check semantic first
							foundOpacity = ( varDesc.Semantic != NULL && ::_stricmp(dx11ShaderSemantic::kOpacity, varDesc.Semantic) == 0 );
							if(foundOpacity == false)
							{
								// Then check annotation
								bool boolValue = 0;
								foundOpacity = (getAnnotation(effectVariable, dx11ShaderSemantic::kOpacity, boolValue) && boolValue);
							}

							if(foundOpacity)
							{
								fOpacityPlugName = param.getPlug().partialName();	// get the plug name, might be different than the variable name
							}
						}
					}
				}
				if (!foundOpacity)
				{
					fErrorLog += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorIsTransparentOpacity );
				}
			}
			break;
		default:
			fErrorLog += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorUnknownIsTransparent );
		}
	}

	/*
	The attribute editor for dx11Shader has a "Default Texture Data"
	section which allows declaring which textured colored channel is
	associated with a texture coordinates declared in the varying
	parameters. This is used by the UV editor to automatically switch
	to the texture that matches the UV set being edited. We want to
	pre-populate these fields with the textured uniform parameter that
	has the lowest "UVEditorOrder" since it represents the preferred
	channel. In the absence of any ordering annotation, we will use
	the first parameter that is a texture.
	*/
	int bestIndex = INT_MAX;
	MString defaultTex;

	unsigned int numUniform = fUniformParameters.length();
	for( unsigned int i = 0; i < numUniform; i++ ) {
		MUniformParameter elem = fUniformParameters.getElement(i);
		if( elem.type() == MUniformParameter::kType2DTexture ) {
			// Skip items which are not UI visible:
			MPlug uniformPlug(elem.getPlug());
			if (uniformPlug.isNull())
				continue;

			MFnAttribute uniformAttribute(uniformPlug.attribute());
			if (uniformAttribute.isHidden())
				continue;

			int currentIndex = bestIndex - numUniform + i;

			ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)elem.userData();
			if (effectVariable)
			{
				int uvEditorOrder;
				if (getAnnotation(effectVariable, "UVEditorOrder", uvEditorOrder))
				{
					currentIndex = uvEditorOrder;
				}
			}

			if (currentIndex < bestIndex)
			{
				bestIndex = currentIndex;
				defaultTex = elem.name();
			}
		}
	}

	if (defaultTex.length() > 0)
	{
		MFnDependencyNode depFn( thisMObject() );
		for (int iVar = 0; iVar < fVaryingParameters.length(); ++iVar)
		{
			MVaryingParameter elem = fVaryingParameters.getElement(iVar);
			if( elem.getSourceType() == MVaryingParameter::kTexCoord ) {
				MString attrName( elem.name() );
				attrName += "_DefaultTexture";
				MPlug defaultTexPlug = depFn.findPlug( attrName );
				if( !defaultTexPlug.isNull() ) {
					defaultTexPlug.setValue( defaultTex );
				}
			}
		}
	}
}

bool dx11ShaderNode::techniqueIsTransparent() const
{
	switch (fTechniqueIsTransparent)
	{
		case eTransparent:
			return true;
		case eOpaque:
			return false;
		case eScriptedTest:
		{
			int result = 0;
			MGlobal::executeCommand(fTransparencyTestProcName + " " + name(), result, false, false);
			return (result == 0 ? false : true);
		}
	}

	// Need to check current opacity value:
	MFnDependencyNode depFn( thisMObject() );
	if (fOpacityPlugName != "")
	{
		MPlug opacityPlug = depFn.findPlug( fOpacityPlugName );

		if( !opacityPlug.isNull() ) {
			float currentOpacity = 1.0f;
			opacityPlug.getValue( currentOpacity );

			return currentOpacity < 1.0f;
		}
	}

	// Assume opaque if opacity plug not found.
	return false;
}

bool dx11ShaderNode::techniqueSupportsAdvancedTransparency() const
{
	return fTechniqueSupportsAdvancedTransparency;
}


// ***********************************
// Pass Management
// ***********************************

dx11ShaderDX11Pass* dx11ShaderNode::activatePass( dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11EffectTechnique* dxTechnique, unsigned int passId, ERenderType renderType ) const
{
	// When called for swatch or UV, we want the color pass:
	MStringArray colorSem;
	colorSem.append(MHWRender::MPassContext::kColorPassSemantic);
	return activatePass( dxDevice, dxContext, dxTechnique, passId, colorSem, renderType );
}

/*
	This method does the main expensive work of setting the active pass.
*/
dx11ShaderDX11Pass* dx11ShaderNode::activatePass( dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11EffectTechnique* dxTechnique,
												  unsigned int passId, const MStringArray& passSem, ERenderType renderType ) const
{
	dx11ShaderDX11Pass* dxPass = dxTechnique->GetPassByIndex(passId);
	if(dxPass == NULL || dxPass->IsValid() == false)
	{
		MStringArray args;
		args.append( MStringFromInt(passId) );
		args.append( fTechniqueName );

		fErrorLog += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorSetPass, args );
		displayErrorAndWarnings();
		return NULL;
	}

	bool canActivate = true;

	MString drawContext;
	getAnnotation(dxPass, "drawContext", drawContext);
	if (drawContext.length())
	{
		// If the shader defines pass contexts, then we must make sure we are in the right one
		// before activating:
		canActivate = false;
		for (unsigned int i=0; i<passSem.length() && !canActivate; i++)
		{
			if (::_stricmp(passSem[i].asChar(), drawContext.asChar()) == 0)
			{
				canActivate = true;
			}
		}
	}

	if (!canActivate)
	{
		return NULL;
	}

	// Get state block mask : identify the states changed by the pass
	D3DX11_STATE_BLOCK_MASK stateBlockMask;
	memset(&stateBlockMask, 0, sizeof(D3DX11_STATE_BLOCK_MASK));
	dxPass->ComputeStateBlockMask(&stateBlockMask);

	// In case the pass modifies the rasterizer state, store the current state description
	D3D11_RASTERIZER_DESC orgRasterizerDesc;
	if(stateBlockMask.RSRasterizerState)
	{
		ID3D11RasterizerState* orgRasterizerState;
		dxContext->RSGetState(&orgRasterizerState);
		orgRasterizerState->GetDesc(&orgRasterizerDesc);
		orgRasterizerState->Release();
	}
	else
	{
		// to fix "potentially uninitialized local variable 'orgRasterizerDesc' used" warning treated as error
		memset(&orgRasterizerDesc, 0, sizeof(D3D11_RASTERIZER_DESC));
	}

	dxPass->Apply(0, dxContext);

	if(stateBlockMask.RSRasterizerState || renderType != RENDER_SCENE)
	{
		// Check new rasterizer state against stored one
		ID3D11RasterizerState* newRasterizerState;
		dxContext->RSGetState(&newRasterizerState);

		D3D11_RASTERIZER_DESC newRasterizerDesc;
		newRasterizerDesc.FillMode = D3D11_FILL_SOLID;
		newRasterizerDesc.CullMode = D3D11_CULL_BACK;
		newRasterizerDesc.FrontCounterClockwise = FALSE;
		newRasterizerDesc.DepthBias = D3D11_DEFAULT_DEPTH_BIAS;
		newRasterizerDesc.DepthBiasClamp = D3D11_DEFAULT_DEPTH_BIAS_CLAMP;
		newRasterizerDesc.SlopeScaledDepthBias = D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		newRasterizerDesc.DepthClipEnable = TRUE;
		newRasterizerDesc.ScissorEnable = FALSE;
		newRasterizerDesc.MultisampleEnable = FALSE;
		newRasterizerDesc.AntialiasedLineEnable = FALSE;

		if (NULL != newRasterizerState)
		{
			newRasterizerState->GetDesc(&newRasterizerDesc);
			newRasterizerState->Release();
		}

		bool createNewRasterizeState = false;

		// SetRasterizerState used in a shader will change the state as a block, and not attribute by attribute.
		// Restore depth attributes to prevent visual issue on the wireframe.
		if( stateBlockMask.RSRasterizerState &&
			( newRasterizerDesc.DepthBias != orgRasterizerDesc.DepthBias ||
			  newRasterizerDesc.SlopeScaledDepthBias != orgRasterizerDesc.SlopeScaledDepthBias ) )
		{
			newRasterizerDesc.DepthBias = orgRasterizerDesc.DepthBias;
			newRasterizerDesc.SlopeScaledDepthBias = orgRasterizerDesc.SlopeScaledDepthBias;
			createNewRasterizeState = true;
		}

		// When a transparent material is managed internaly by Maya,
		// it is rendered twice : once for back and again for front
		// the effect should not override the cull mode
		if( renderType == RENDER_SCENE &&
			techniqueIsTransparent() &&
			fTechniqueOverridesDrawState == false &&
			( newRasterizerDesc.CullMode != orgRasterizerDesc.CullMode ||
			  newRasterizerDesc.FrontCounterClockwise != orgRasterizerDesc.FrontCounterClockwise ) )
		{
			newRasterizerDesc.CullMode = orgRasterizerDesc.CullMode;
			newRasterizerDesc.FrontCounterClockwise = orgRasterizerDesc.FrontCounterClockwise;
			createNewRasterizeState = true;
		}

		// Force back culling for swatch and uv texture render
		if( renderType != RENDER_SCENE &&
			newRasterizerDesc.FillMode == D3D11_FILL_SOLID &&
			newRasterizerDesc.CullMode == D3D11_CULL_NONE )
		{
			newRasterizerDesc.CullMode = D3D11_CULL_BACK;
			newRasterizerDesc.FrontCounterClockwise = true;
			createNewRasterizeState = true;
		}

		if( createNewRasterizeState && SUCCEEDED( dxDevice->CreateRasterizerState( &newRasterizerDesc, &newRasterizerState ) ) )
		{
 			dxContext->RSSetState( newRasterizerState );
			newRasterizerState->Release();
		}
	}

	if(renderType != RENDER_SCENE)
	{
		// Swatches require the DestBlendAlpha value to be ONE otherwise
		// we end up with a completely transparent swatch showing the nice
		// gray underneath the swatch.
		ID3D11BlendState* newBlendState;
		FLOAT newBlendFactor[4];
		UINT newSampleMask;
		dxContext->OMGetBlendState(&newBlendState, newBlendFactor, &newSampleMask);

		D3D11_BLEND_DESC newBlendDesc;
		newBlendState->GetDesc(&newBlendDesc);
		newBlendState->Release();

		bool createNewBlendState = false;

		// Check first renderTarget only
		if( newBlendDesc.RenderTarget[0].BlendEnable != FALSE &&
			newBlendDesc.RenderTarget[0].DestBlendAlpha != D3D11_BLEND_ONE )
		{
			newBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
			createNewBlendState = true;
		}

		if( createNewBlendState && SUCCEEDED( dxDevice->CreateBlendState( &newBlendDesc, &newBlendState ) ) )
		{
			dxContext->OMSetBlendState(newBlendState, newBlendFactor, newSampleMask);
			newBlendState->Release();
		}
	}

	return dxPass;
}

bool dx11ShaderNode::passHasHullShader(dx11ShaderDX11Pass* dxPass) const
{
	PassHasHullShaderMap::const_iterator it = fPassHasHullShaderMap.find(dxPass);
	if(it != fPassHasHullShaderMap.end())
		return it->second;

	D3DX11_PASS_SHADER_DESC hullShaderDesc;
	HRESULT hr = dxPass->GetHullShaderDesc(&hullShaderDesc);

	bool bContainsHullShader = false;
	if(SUCCEEDED(hr) && hullShaderDesc.pShaderVariable && hullShaderDesc.pShaderVariable->IsValid())
	{
		// The most recent Effect11 library will return a pointer to an empty shader
		// so we need to make sure there is actual bytecode before we ask for the
		// shader itself.
		D3DX11_EFFECT_SHADER_DESC hullEffectDesc;
		hr = hullShaderDesc.pShaderVariable->GetShaderDesc(hullShaderDesc.ShaderIndex,&hullEffectDesc);

		if (SUCCEEDED(hr) && hullEffectDesc.BytecodeLength) // This will not work if Optimize() has been called.
		{
			ID3D11HullShader* pHullShader = NULL;
			hullShaderDesc.pShaderVariable->GetHullShader(hullShaderDesc.ShaderIndex,&pHullShader);
			if(pHullShader)
			{
				bContainsHullShader = true;
				pHullShader->Release();
			}
		}
	}

	fPassHasHullShaderMap[dxPass] = bContainsHullShader;

	return bContainsHullShader;
}

dx11ShaderDX11InputLayout* dx11ShaderNode::getInputLayout(dx11ShaderDX11Device* dxDevice, dx11ShaderDX11Pass* dxPass, unsigned int numLayouts, const dx11ShaderDX11InputElementDesc* layoutDesc) const
{
	PassInputLayoutMap::iterator it = fPassInputLayoutMap.find(dxPass);
	if(it != fPassInputLayoutMap.end())
	{
		// Already in cache check if still valid
		InputLayoutData& data = it->second;

		if( numLayouts == data.numLayouts )
		{
			bool isEqual = true;
			for(unsigned int i = 0; isEqual && i < numLayouts; ++i)
			{
				const CachedInputElementDesc &haveDesc = data.layoutDesc[i];
				const dx11ShaderDX11InputElementDesc &wantDesc = layoutDesc[i];

				isEqual = ( haveDesc.SemanticIndex == wantDesc.SemanticIndex &&		// Check int and enum values first, string last
							haveDesc.Format == wantDesc.Format &&
							haveDesc.InputSlot == wantDesc.InputSlot &&
							haveDesc.AlignedByteOffset == wantDesc.AlignedByteOffset &&
							haveDesc.InputSlotClass == wantDesc.InputSlotClass &&
							haveDesc.InstanceDataStepRate == wantDesc.InstanceDataStepRate &&
							strcmp(haveDesc.SemanticName.asChar(), wantDesc.SemanticName) == 0 );
			}

			if(isEqual)
				return data.inputLayout;
		}

		// Was not valid, flush from cache
		data.inputLayout->Release();
		delete [] data.layoutDesc;
		fPassInputLayoutMap.erase(it);
	}

	D3DX11_PASS_DESC descPass;
	dxPass->GetDesc(&descPass);

	ID3D11InputLayout* inputLayout = NULL;
	dxDevice->CreateInputLayout(layoutDesc, numLayouts, descPass.pIAInputSignature, descPass.IAInputSignatureSize, &inputLayout);

	// Cache the new layout
	if(inputLayout != NULL)
	{
		InputLayoutData data;
		data.inputLayout = inputLayout;
		data.numLayouts = numLayouts;
		data.layoutDesc = new CachedInputElementDesc[numLayouts];

		for(unsigned int i = 0; i < numLayouts; ++i)
		{
			const dx11ShaderDX11InputElementDesc &wantDesc = layoutDesc[i];
			CachedInputElementDesc &cacheDesc = data.layoutDesc[i];

			cacheDesc.SemanticIndex = wantDesc.SemanticIndex;
			cacheDesc.Format = wantDesc.Format;
			cacheDesc.InputSlot = wantDesc.InputSlot;
			cacheDesc.AlignedByteOffset = wantDesc.AlignedByteOffset;
			cacheDesc.InputSlotClass = wantDesc.InputSlotClass;
			cacheDesc.InstanceDataStepRate = wantDesc.InstanceDataStepRate;
			cacheDesc.SemanticName = MString(wantDesc.SemanticName);
		}

		fPassInputLayoutMap[dxPass] = data;
	}

	return inputLayout;
}

// ***********************************
// Rendering
// ***********************************

MStatus	dx11ShaderNode::render( MGeometryList& iterator)
{
	MStatus result = MStatus::kFailure;

	glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
	glPushAttrib(GL_CURRENT_BIT);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glColor4f(0.7f, 0.1f, 0.1f, 1.0f);
	glDisable(GL_LIGHTING);

	for( ; iterator.isDone() == false; iterator.next())
	{
		MGeometry& geometry = iterator.geometry( MGeometryList::kMatrices );

		{
			const MGeometryData position = geometry.position();

			GLint size = 0;
			switch (position.elementSize())
			{
			case MGeometryData::kOne:	size = 1; break;
			case MGeometryData::kTwo:	size = 2; break;
			case MGeometryData::kThree:	size = 3; break;
			case MGeometryData::kFour:	size = 4; break;
			default:					continue;
			}
			const GLvoid* data = position.data();

			glVertexPointer(size, GL_FLOAT, 0, data);
		}
		{
			const MGeometryData normal = geometry.normal();

			const GLvoid* data = normal.data();

			glNormalPointer(GL_FLOAT, 0, data);
		}
		for(unsigned int primitiveIdx = 0; primitiveIdx < geometry.primitiveArrayCount(); ++primitiveIdx)
		{
			MGeometryPrimitive primitive = geometry.primitiveArray(primitiveIdx);

			GLenum mode = GL_TRIANGLES;
			switch (primitive.drawPrimitiveType())
			{
			case MGeometryPrimitive::kPoints:			mode = GL_POINTS;			break;
			case MGeometryPrimitive::kLines:			mode = GL_LINES;			break;
			case MGeometryPrimitive::kLineStrip:		mode = GL_LINE_STRIP;		break;
			case MGeometryPrimitive::kLineLoop:			mode = GL_LINE_LOOP;		break;
			case MGeometryPrimitive::kTriangles:		mode = GL_TRIANGLES;		break;
			case MGeometryPrimitive::kTriangleStrip:	mode = GL_TRIANGLE_STRIP;	break;
			case MGeometryPrimitive::kTriangleFan:		mode = GL_TRIANGLE_FAN;		break;
			case MGeometryPrimitive::kQuads:			mode = GL_QUADS;			break;
			case MGeometryPrimitive::kQuadStrip:		mode = GL_QUAD_STRIP;		break;
			case MGeometryPrimitive::kPolygon:			mode = GL_POLYGON;			break;
			default:									continue;
			};
			GLenum format = GL_UNSIGNED_INT;
			switch (primitive.dataType())
			{
			case MGeometryData::kChar:
			case MGeometryData::kUnsignedChar:			format = GL_UNSIGNED_BYTE;	break;
			case MGeometryData::kInt16:
			case MGeometryData::kUnsignedInt16:			format = GL_UNSIGNED_SHORT;	break;
			case MGeometryData::kInt32:
			case MGeometryData::kUnsignedInt32:			format = GL_UNSIGNED_INT;	break;
			default:									continue;
			}
			GLsizei count = primitive.elementCount();
			const GLvoid* indices = primitive.data();
			glDrawElements(mode, count, format, indices);
			result = MStatus::kSuccess; // something drew
		}
	}

	glPopAttrib();
	glPopClientAttrib();

	return result;
}

/*
	Renders a representation of the active effect/technique to display in the attribute editor.

	The geometry (a sphere) is rendered in DX in a texture target, using the same pipeline as the viewport render
	the texture target is then blit to the output image.

	If there is no valid effect/technique, a simple shader is compiled and used to offer a dummy representation of the shader.

	The geometry buffers are retrieved from MGeometryUtilities,
	and currently need to be manually altered if the active technique needs any custom indexing.
	This is the case for the crack free tessellation (PNAEN9 and PNAEN18 index buffer types).
	This is done in renderPass().
*/
MStatus dx11ShaderNode::renderSwatchImage( MImage & image )
{
	// Get device
	MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
	if (!theRenderer || theRenderer->drawAPIIsOpenGL()) return MStatus::kFailure;

	const MHWRender::MRenderTargetManager* targetManager = theRenderer->getRenderTargetManager();
	if (!targetManager) return MStatus::kFailure;

	ID3D11Device* dxDevice = (ID3D11Device*)theRenderer->GPUDeviceHandle();
	if (!dxDevice) return MStatus::kFailure;

	MHWRender::MDrawContext *context = MHWRender::MRenderUtilities::acquireSwatchDrawContext();
	if (!context) return MStatus::kFailure;

	unsigned int width, height;
	image.getSize(width, height);

	// If no valid effect/technique/pass, create a temporary effect to use
	ID3DX11Effect *dxEffect = NULL;
	ID3DX11EffectTechnique *dxTechnique = fTechnique;

	// Use local parameters lists to switch between loaded effect and temporary effect
	MUniformParameterList* uniformParameters = &fUniformParameters;
	MVaryingParameterList* varyingParameters = &fVaryingParameters;
	ResourceTextureMap* resourceTexture = &fResourceTextureMap;
	MString indexBufferType = fTechniqueIndexBufferType;

	unsigned int numPasses = fPassCount;
	ERenderType renderType = RENDER_SWATCH;
	if(numPasses == 0 || dxTechnique == NULL || dxTechnique->IsValid() == false || (fUniformParameters.length() == 0 && fVaryingParameters.length() == 0))
	{
		static const char* simpleShaderCode =	"// transform object vertices to view space and project them in perspective: \r\n" \
												"float4x4 gWvpXf : WorldViewProjection; \r\n" \

												"struct appdata \r\n" \
												"{ \r\n" \
												"	float3 Pos : POSITION; \r\n" \
												"	float4 Color : COLOR0; \r\n" \
												"}; \r\n" \

												"struct vertexOutput \r\n" \
												"{ \r\n" \
												"	float4 Pos : POSITION; \r\n" \
												"	float4 Color : COLOR0; \r\n" \
												"}; \r\n" \

												"vertexOutput BasicVS(appdata IN, uniform float4x4 WvpXf) \r\n" \
												"{ \r\n" \
												"	vertexOutput OUT; \r\n" \
												"	float4 Po = float4(IN.Pos,1); \r\n" \
												"	OUT.Pos = mul(Po,WvpXf); \r\n" \
												"	OUT.Color = IN.Color; \r\n" \
												"	return OUT; \r\n" \
												"} \r\n" \

												"float4 BasicPS(vertexOutput IN) : COLOR \r\n" \
												"{ \r\n" \
												"	return IN.Color; \r\n" \
												"} \r\n" \

												"technique10 simple \r\n" \
												"{ \r\n" \
												"	pass p0 \r\n" \
												"	{ \r\n" \
												"		SetVertexShader( CompileShader( vs_4_0, BasicVS(gWvpXf) ) ); \r\n" \
												"		SetGeometryShader( NULL ); \r\n" \
												"		SetPixelShader( CompileShader( ps_4_0, BasicPS() ) ); \r\n" \
												"	} \r\n" \
												"} \r\n";
		static const unsigned int simpleShaderLength = (unsigned int)strlen(simpleShaderCode);

		// Create a new effect, as well as new varyingParameters and uniformParameters lists
		buildTemporaryEffect(this,
					dxDevice, simpleShaderCode, simpleShaderLength,
					dxEffect, dxTechnique, numPasses,
					varyingParameters, uniformParameters, indexBufferType);

		renderType = RENDER_SWATCH_PROXY;
		resourceTexture = new ResourceTextureMap;
	}

	MStatus result = MStatus::kFailure;
	if(numPasses > 0)
	{
		// Get geometry
		MHWRender::MGeometry* geometry = acquireReferenceGeometry( MHWRender::MGeometryUtilities::kDefaultSphere, *varyingParameters );
		if(geometry != NULL)
		{
			// Create texture target
			MHWRender::MRenderTargetDescription textureDesc( MString("dx11Shader_swatch_texture_target"), width, height, 0, MHWRender::kR8G8B8A8_UNORM, 1, false);
			MHWRender::MRenderTarget* textureTarget = targetManager->acquireRenderTarget(textureDesc);
			if(textureTarget != NULL)
			{
				updateParameters(*context, *uniformParameters, *resourceTexture, renderType);

				float clearColor[4]; // = { 1.0f, 0.0f, 0.0f, 1.0f };
				MHWRender::MRenderUtilities::swatchBackgroundColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );

				// render geometry to texture target
				if( renderTechnique(dxDevice, dxTechnique, numPasses,
									textureTarget, width, height, clearColor,
									geometry, MHWRender::MGeometry::kTriangles, 3,
									*varyingParameters, renderType, indexBufferType) )
				{
					// At this point we have the drawing in the target texture
					// blit texture target to swatch image
					result = MHWRender::MRenderUtilities::blitTargetToImage(textureTarget, image);
				}

				targetManager->releaseRenderTarget(textureTarget);
			}

			MHWRender::MGeometryUtilities::releaseReferenceGeometry( geometry );
		}
	}

	// Temporary effect
	if(dxEffect)
	{
		CDX11EffectCompileHelper::releaseEffect(this, dxEffect, "TemporaryEffect");

		// The parameters lists were created for the temporary effect
		delete uniformParameters;
		delete varyingParameters;

		// As was the resource texture
		releaseAllTextures(*resourceTexture);
		delete resourceTexture;
	}

	MHWRender::MRenderUtilities::releaseDrawContext( context );

	return result;
}

// Override this method to support texture display in the UV texture editor.
MStatus dx11ShaderNode::getAvailableImages( const MPxHardwareShader::ShaderContext &context,const MString &uvSetName,MStringArray &imageNames )
{
	// Locate the varying parameters whose source is 'uvSetName'
	MStringArray uvParams;
	MString		 uvLocalName = uvSetName=="" ? "map1" : uvSetName;

	unsigned int nVarying = fVaryingParameters.length();
	for( unsigned int i = 0; i < nVarying; i++ ) {
		MVaryingParameter elem = fVaryingParameters.getElement(i);
		if( elem.getSourceType() == MVaryingParameter::kTexCoord && elem.getSourceSetName() == uvLocalName ) {
			uvParams.append( elem.name() );
		}
	}


	// Determine the default texture.
	//
	MString defaultTex;
	if( uvParams.length() > 0 ) {
		// Only process the first entry of this UV set (if multiple exist).
		// There can only be one default, so we'll only consider the default
		// of the first varying input of this UV set.
		//
		MFnDependencyNode depFn( thisMObject() );
		MString attrName( uvParams[0] );
		attrName += "_DefaultTexture";
		MPlug defaultTexPlug = depFn.findPlug( attrName );
		if( !defaultTexPlug.isNull() ) {
			defaultTexPlug.getValue( defaultTex );
		}
	}


	// Locate any texture UVLinks that point to these uvParams and record
	// those textures.
	// If no UVLinks found, display all 2D textures.
	//
	std::multimap<int, MString> sortedTextures;
	std::vector<MString> unsortedTextures;
	unsigned int nUniform = fUniformParameters.length();
	if( imageNames.length() == 0 ) {
		for( unsigned int i = 0; i < nUniform; i++ ) {
			MUniformParameter elem = fUniformParameters.getElement(i);
			if( elem.type() == MUniformParameter::kType2DTexture ) {
				// Skip items which are not UI visible:
				MPlug uniformPlug(elem.getPlug());
				if (uniformPlug.isNull())
					continue;

				MFnAttribute uniformAttribute(uniformPlug.attribute());
				if (uniformAttribute.isHidden())
					continue;

				ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)elem.userData();
				if (effectVariable)
				{
					int uvEditorOrder;
					if (getAnnotation(effectVariable, "UVEditorOrder", uvEditorOrder))
					{
						sortedTextures.insert(std::pair<int, MString>(uvEditorOrder, elem.name()));
						continue;
					}
				}
				unsortedTextures.push_back(elem.name());
			}
		}
	}

	// First copy items that are ordered:
	for (std::multimap<int, MString>::iterator itSorted = sortedTextures.begin();
		 itSorted != sortedTextures.end();
		 ++itSorted)
	{
		MString &elemName(itSorted->second);
		if( elemName == defaultTex ) {
			imageNames.insert( elemName, 0 );
		} else {
			imageNames.append( elemName );
		}
	}

	// Then append unordered items:
	for (std::vector<MString>::iterator itOther = unsortedTextures.begin();
		 itOther != unsortedTextures.end();
		 ++itOther)
	{
		MString &elemName(*itOther);
		if( elemName == defaultTex ) {
			imageNames.insert( elemName, 0 );
		} else {
			imageNames.append( elemName );
		}
	}

	return (imageNames.length() > 0) ? MStatus::kSuccess : MStatus::kNotImplemented;
}

/*
	Renders the specified texture (imageName) to the UV editor.

	The texture is rendered in DX using a simple effect.
	When creating the effect a temporary uniform and varying parameters lists are created accordingly,
	so we can use the same pipeline as when rendering the material in viewport 2.0

	The UV editor uses an OpenGL viewport, therefore we first render the texture
	to a DX texture target and then blit the result to GL.

	To increase the performance of the UV editor, instead of rendering and blitting to GL on each call,
	the result GL texture is cached and reused as long as possible.
*/
MStatus dx11ShaderNode::renderImage( const MPxHardwareShader::ShaderContext& shaderContext, const MString& imageName, floatRegion region, const MPxHardwareShader::RenderParameters& parameters, int& imageWidth, int& imageHeight )
{
	// Get device
	MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
	if (!theRenderer || theRenderer->drawAPIIsOpenGL())
		return MStatus::kFailure;

	ID3D11Device* dxDevice = (ID3D11Device*)theRenderer->GPUDeviceHandle();
	if (!dxDevice) return MStatus::kFailure;

	MHWRender::MDrawContext *context = MHWRender::MRenderUtilities::acquireUVTextureDrawContext();
	if (!context) return MStatus::kFailure;

	MString textureName, layerName;
  int alphaChannelIdx, mipmapLevels;
  MHWRender::MTexture* texture = getUVTexture(context, imageName, imageWidth, imageHeight, textureName, layerName, alphaChannelIdx, mipmapLevels);

	if(texture == NULL)
	{
		MHWRender::MRenderUtilities::releaseDrawContext( context );
		return MStatus::kNotImplemented;
	}

	// Early return, this is just a call to get the size of the texture ("Use image ratio" is on)
	if(region[0][0] == 0 && region[0][1] == 0 && region[1][0] == 0 && region[1][1] == 0)
	{
		MHWRender::MRenderUtilities::releaseDrawContext( context );
		return MStatus::kSuccess;
	}

	// Retrieve data from render parameters
	float baseColor[4] = {parameters.baseColor.r, parameters.baseColor.g, parameters.baseColor.b, parameters.baseColor.a };
	bool unfiltered = parameters.unfiltered;
	bool showAlphaMask = parameters.showAlphaMask;

#ifdef USE_GL_TEXTURE_CACHING
	// Try with cached GL texture
	if(fUVEditorGLTextureId > 0 &&
		fUVEditorLastTexture == textureName &&
		fUVEditorLastLayer == layerName &&
    fUVEditorLastAlphaChannel == alphaChannelIdx &&
		fUVEditorShowAlphaMask == showAlphaMask &&
		fUVEditorBaseColor[0] == baseColor[0] &&
		fUVEditorBaseColor[1] == baseColor[1] &&
		fUVEditorBaseColor[2] == baseColor[2] &&
		fUVEditorBaseColor[3] == baseColor[3] )
	{
		MHWRender::MRenderUtilities::releaseDrawContext( context );
		MStatus result = MStatus::kFailure;
		if( renderGLTexture(fUVEditorGLTextureId, fUVEditorGLTextureScaleU, fUVEditorGLTextureScaleV, region, unfiltered) )
			result = MStatus::kSuccess;
		return result;
	}
	// Cached GL texture out of date, release it
	if(fUVEditorGLTextureId > 0)
	{
		releaseGLTexture(fUVEditorGLTextureId);
		fUVEditorGLTextureId = 0;
	}
	fUVEditorLastTexture = textureName;
	fUVEditorLastLayer = layerName;
  fUVEditorLastAlphaChannel = alphaChannelIdx;
	fUVEditorShowAlphaMask = showAlphaMask;
	fUVEditorBaseColor[0] = baseColor[0];
	fUVEditorBaseColor[1] = baseColor[1];
	fUVEditorBaseColor[2] = baseColor[2];
	fUVEditorBaseColor[3] = baseColor[3];
	fUVEditorGLTextureScaleU = fUVEditorGLTextureScaleV = 1.0f;
#endif //USE_GL_TEXTURE_CACHING

	const MHWRender::MRenderTargetManager* targetManager = theRenderer->getRenderTargetManager();
	if (!targetManager) return MStatus::kFailure;

	// If no valid effect/technique/pass, create a temporary effect to use
	ID3DX11Effect *dxEffect = NULL;
	ID3DX11EffectTechnique *dxTechnique = NULL;

	// Use local parameters lists to switch between loaded effect and temporary effect
	MUniformParameterList* uniformParameters = NULL;
	MVaryingParameterList* varyingParameters = NULL;
	ResourceTextureMap* resourceTexture = NULL;
	MString indexBufferType;

	// Create effect
	unsigned int numPasses = 0;
	ERenderType renderType = RENDER_UVTEXTURE;
	{
		static const char* simpleShaderCode =	"float4x4 gWvpXf : WorldViewProjection; \r\n" \

												"SamplerState SamplerLinear \r\n" \
												"{ \r\n" \
												"	Filter = MIN_MAG_MIP_LINEAR; \r\n" \
												"	AddressU = Wrap; \r\n" \
												"	AddressV = Wrap; \r\n" \
												"}; \r\n" \

												"Texture2D myTexture; \r\n" \

												"float4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f }; \r\n" \
												"bool showAlphaMask = false; \r\n" \

												"struct appdata \r\n" \
												"{ \r\n" \
												"	float3 Pos : POSITION; \r\n" \
												"	float2 Uv : TEXTCOORD; \r\n" \
												"}; \r\n" \

												"struct vertexOutput \r\n" \
												"{ \r\n" \
												"	float4 Pos : POSITION; \r\n" \
												"	float2 Uv : TEXTCOORD; \r\n" \
												"}; \r\n" \

												"vertexOutput BasicVS(appdata IN, uniform float4x4 WvpXf) \r\n" \
												"{ \r\n" \
												"	vertexOutput OUT; \r\n" \
												"	float4 Po = float4(IN.Pos,1); \r\n" \
												"	OUT.Pos = mul(Po,WvpXf); \r\n" \
												"	OUT.Uv = IN.Uv; \r\n" \
												"	return OUT; \r\n" \
												"} \r\n" \

												"float4 BasicPS(vertexOutput IN) : COLOR \r\n" \
												"{ \r\n" \
												"	float4 color = myTexture.Sample(SamplerLinear, IN.Uv); \r\n" \
												"	color *= baseColor; \r\n" \
												"	if(showAlphaMask) \r\n" \
												"		color = float4(color.www, 1.0f); \r\n" \
												"	return color; \r\n" \
												"} \r\n" \

												"technique10 simple \r\n" \
												"{ \r\n" \
												"	pass p0 \r\n" \
												"	{ \r\n" \
												"		SetVertexShader( CompileShader( vs_4_0, BasicVS(gWvpXf) ) ); \r\n" \
												"		SetGeometryShader( NULL ); \r\n" \
												"		SetPixelShader( CompileShader( ps_4_0, BasicPS() ) ); \r\n" \
												"	} \r\n" \
												"} \r\n";
		static const unsigned int simpleShaderLength = (unsigned int)strlen(simpleShaderCode);

		// Create a new effect, as well as new varyingParameters and uniformParameters lists
		buildTemporaryEffect(this,
					dxDevice, simpleShaderCode, simpleShaderLength,
					dxEffect, dxTechnique, numPasses,
					varyingParameters, uniformParameters, indexBufferType);

		resourceTexture = new ResourceTextureMap;

		// We use a custom effect here
		// set a fixed mipmap levels so we load the right texture :
		// - consistency : same quality between the UV editor and the scene
		// - performance : will use the cached texture instead of loading a different version
		fFixedTextureMipMapLevels = mipmapLevels;
	}

	// Push texture and parameters to uniform parameters
	for( int u = 0; u < uniformParameters->length(); ++u )
	{
		MUniformParameter uniform = uniformParameters->getElement(u);
		if( uniform.isATexture() )
		{
			if( uniform.name() == MString("myTexture") ) {
				MString fullName = textureName;
				if(alphaChannelIdx != -1 || layerName.length() > 0) {
					fullName += MString(&layerNameSeparator, 1) + layerName;
                    fullName += MString(&layerNameSeparator, 1) + alphaChannelIdx;
                }
				uniform.setAsString(fullName);
			}
		}
		else if( uniform.type() == MUniformParameter::kTypeFloat && uniform.numColumns() == 4 && uniform.numRows() == 1 )
		{
			if( uniform.name() == MString("baseColor") )
				uniform.setAsFloatArray(baseColor, 4);
		}
		else if( uniform.type() == MUniformParameter::kTypeBool && uniform.numColumns() == 1 && uniform.numRows() == 1 )
		{
			if( uniform.name() == MString("showAlphaMask") )
				uniform.setAsBool(showAlphaMask);
		}
	}

	MStatus result = MStatus::kFailure;
	if(numPasses > 0)
	{
		// Get geometry
		MHWRender::MGeometry* geometry = acquireReferenceGeometry( MHWRender::MGeometryUtilities::kDefaultPlane, *varyingParameters );
		if(geometry != NULL)
		{
			// Create texture target
			MHWRender::MRenderTargetDescription textureDesc( MString("dx11Shader_uv_texture_target"), imageWidth, imageHeight, 0, MHWRender::kR8G8B8A8_UNORM, 1, false);
			MHWRender::MRenderTarget* textureTarget = targetManager->acquireRenderTarget(textureDesc);
			if(textureTarget != NULL)
			{
				updateParameters(*context, *uniformParameters, *resourceTexture, renderType);

				float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

				// render geometry to texture target
				if( renderTechnique(dxDevice, dxTechnique, numPasses,
									textureTarget, imageWidth, imageHeight, clearColor,
									geometry, MHWRender::MGeometry::kTriangles, 3,
									*varyingParameters, renderType, indexBufferType) )
				{
					// At this point we have the drawing in the target texture
					// blit texture to GL
#ifdef USE_GL_TEXTURE_CACHING
					fUVEditorGLTextureId = createGLTextureFromTarget(textureTarget, fUVEditorGLTextureScaleU, fUVEditorGLTextureScaleV);
					if(fUVEditorGLTextureId > 0 && renderGLTexture(fUVEditorGLTextureId, fUVEditorGLTextureScaleU, fUVEditorGLTextureScaleV, region, unfiltered) )
						result = MStatus::kSuccess;
#else
					result = MHWRender::MRenderUtilities::blitTargetToGL(textureTarget, region, unfiltered);
#endif //USE_GL_TEXTURE_CACHING

				}

				targetManager->releaseRenderTarget(textureTarget);
			}

			MHWRender::MGeometryUtilities::releaseReferenceGeometry( geometry );
		}
	}

	// Temporary effect
	if(dxEffect)
	{
		CDX11EffectCompileHelper::releaseEffect(this, dxEffect, "TemporaryEffect");

		// The parameters lists were created for the temporary effect
		delete uniformParameters;
		delete varyingParameters;

		// As was the resource texture
		releaseAllTextures(*resourceTexture);
		delete resourceTexture;
	}

	// Reset the fixed levels
	fFixedTextureMipMapLevels = -1;

	MHWRender::MRenderUtilities::releaseDrawContext( context );

	return result;
}

/*
	Renders the specified texture (imageName) to the UV editor in viewport 2.0.
*/
MStatus dx11ShaderNode::renderImage( const MPxHardwareShader::ShaderContext& shaderContext, MHWRender::MUIDrawManager& uiDrawManager, const MString& imageName, floatRegion region, const MPxHardwareShader::RenderParameters& parameters, int& imageWidth, int& imageHeight )
{
    // Get device
    MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
    if (!theRenderer || theRenderer->drawAPIIsOpenGL())
        return MStatus::kFailure;

    ID3D11Device* dxDevice = (ID3D11Device*)theRenderer->GPUDeviceHandle();
    if (!dxDevice) return MStatus::kFailure;

    MHWRender::MDrawContext *context = MHWRender::MRenderUtilities::acquireUVTextureDrawContext();
    if (!context) return MStatus::kFailure;

    MHWRender::MTexture* texture = getUVTexture(context, imageName, imageWidth, imageHeight);

    if(texture == NULL)
    {
        MHWRender::MRenderUtilities::releaseDrawContext( context );
        return MStatus::kNotImplemented;
    }

    // Early return, this is just a call to get the size of the texture ("Use image ratio" is on)
    if(region[0][0] == 0 && region[0][1] == 0 && region[1][0] == 0 && region[1][1] == 0)
    {
        MHWRender::MRenderUtilities::releaseDrawContext( context );
        return MStatus::kSuccess;
    }

    // Render texture on quad
    MPointArray positions;
    MPointArray& texcoords = positions;

    // Tri #0
    positions.append(region[0][0], region[0][1]);
    positions.append(region[1][0], region[0][1]);
    positions.append(region[1][0], region[1][1]);

    // Tri #1
    positions.append(region[0][0], region[0][1]);
    positions.append(region[1][0], region[1][1]);
    positions.append(region[0][0], region[1][1]);

    uiDrawManager.setColor( parameters.baseColor );
    uiDrawManager.setTexture( texture );
    uiDrawManager.setTextureSampler( parameters.unfiltered ? MHWRender::MSamplerState::kMinMagMipLinear : MHWRender::MSamplerState::kMinMagMipPoint, MHWRender::MSamplerState::kTexWrap );
    uiDrawManager.setTextureMask( parameters.showAlphaMask ? MHWRender::MBlendState::kAlphaChannel : MHWRender::MBlendState::kRGBAChannels );
    uiDrawManager.mesh( MHWRender::MUIDrawManager::kTriangles, positions, NULL, NULL, NULL, &texcoords );
    uiDrawManager.setTexture( NULL );

    return MStatus::kSuccess;
}

MHWRender::MTexture* dx11ShaderNode::getUVTexture(MHWRender::MDrawContext *context, const MString& imageName, int& imageWidth, int& imageHeight)
{
  MString textureName, layerName;
  int alphaChannelIdx, mipmapLevels;
  return getUVTexture(context, imageName, imageWidth, imageHeight, textureName, layerName, alphaChannelIdx, mipmapLevels);
}

MHWRender::MTexture* dx11ShaderNode::getUVTexture(MHWRender::MDrawContext *context, const MString& imageName, int& imageWidth, int& imageHeight, MString &textureName, MString& layerName, int &alphaChannelIdx, int &mipmapLevels)
{
	MUniformParameter imageParam;
	unsigned int nUniform = fUniformParameters.length();
	for( unsigned int i = 0; i < nUniform; i++ ) {
		MUniformParameter elem = fUniformParameters.getElement(i);
		if( elem.isATexture() && elem.name() == imageName ) { // Check for isATexture, as multiple parameters can have the same UI name.
			imageParam = elem;
			break;
		}
	}

	// Only supports 2D textures.
	//
	if( imageParam.type() != MUniformParameter::kType2DTexture ) {
		return NULL;
	}

	// It happens that the UV editor get rendered before the viewport (or the swatch)
	// Retrieving the texture string value will clear the hasChanged flag
	// And when the viewport will get rendered, the texture will not be properly updated to the effect
	if (imageParam.hasChanged(*context))
		fForceUpdateTexture = true;

	// Get texture
	getTextureDesc(*context, imageParam, textureName, layerName, alphaChannelIdx);
	MHWRender::MTexture* texture = NULL;
	mipmapLevels = 1;
	{
		// Generate mip map levels desired by technique
		mipmapLevels = fTechniqueTextureMipMapLevels;
		// If the texture itself specify a level, it prevails over the technique's
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)imageParam.userData();
		if(effectVariable)
		{
			ID3DX11EffectShaderResourceVariable* resourceVar = effectVariable->AsShaderResource();
			if(resourceVar)
				getAnnotation(resourceVar, dx11ShaderAnnotation::kMipmaplevels, mipmapLevels);
		}

		texture = loadTexture(textureName, layerName, alphaChannelIdx, mipmapLevels);
	}

	// Release texture used for previous uv editor render and store the new one.
	// This is helpful if the scene does not render the texture.
	// This prevent having to load the same texture again and again on each draw
	releaseTexture(fUVEditorTexture);
	fUVEditorTexture = texture;

  if(texture)
	{
		MHWRender::MTextureDescription desc;
		texture->textureDescription(desc);

		imageWidth  = (int)desc.fWidth;
		imageHeight = (int)desc.fHeight;
	}
  
  return texture;
}

/*
	Render all the geometry within the renderItemList using active technique
	This is called to render the geometry in the viewport 2.0

	Split the render items in 2 lists; the items that can receive shadows and the ones that can't.
	Render both lists against the selected technique.
*/
bool dx11ShaderNode::render(const MHWRender::MDrawContext& context, const MHWRender::MRenderItemList& renderItemList)
{
	if(fTechnique == NULL || fTechnique->IsValid() == false)
		return false;

	// Get device
	MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
	if (!theRenderer) return false;
	ID3D11Device* dxDevice = (ID3D11Device*)theRenderer->GPUDeviceHandle();
	if (!dxDevice) return false;

	// Get context
	ID3D11DeviceContext* dxContext = NULL;
	dxDevice->GetImmediateContext(&dxContext);
	if (!dxContext) return false;

	ERenderType renderType = RENDER_SCENE;

	// Update shader parameters
	updateParameters(context, fUniformParameters, fResourceTextureMap, renderType);

	// These will hold the global and per-light state
	// while we toggle the per-geometry state:
	TshadowFlagBackupState shadowFlagBackupState;
	initShadowFlagBackupState(shadowFlagBackupState);

	// We can now render in different context:
	const MHWRender::MPassContext & passCtx = context.getPassContext();
	const MStringArray & passSem = passCtx.passSemantics();

	// Draw (return true if we manage to draw anything, not necessarily everything)
	bool result = false;

	// Split items with shadows from items without, only if necessary.
	RenderItemList shadowOnRenderVec, shadowOffRenderVec;

	int numRenderItems = renderItemList.length();
	for (int renderItemIdx=0; renderItemIdx < numRenderItems; ++renderItemIdx)
	{
		const MHWRender::MRenderItem* renderItem = renderItemList.itemAt(renderItemIdx);
		if (renderItem)
		{
			if (renderItem->receivesShadows() || shadowFlagBackupState.empty())
				shadowOnRenderVec.push_back(renderItem);
			else
				shadowOffRenderVec.push_back(renderItem);
		}
	}

	if (!shadowOnRenderVec.empty())
	{
		if (!shadowFlagBackupState.empty())
			setPerGeometryShadowOnFlag(true, shadowFlagBackupState);
		result |= renderTechnique(dxDevice, dxContext, fTechnique, fPassCount, passSem, shadowOnRenderVec, fVaryingParameters, renderType, fTechniqueIndexBufferType);
	}

	if (!shadowOffRenderVec.empty())
	{
		if (!shadowFlagBackupState.empty())
			setPerGeometryShadowOnFlag(false, shadowFlagBackupState);
		result |= renderTechnique(dxDevice, dxContext, fTechnique, fPassCount, passSem, shadowOffRenderVec, fVaryingParameters, renderType, fTechniqueIndexBufferType);
	}

	dxContext->Release();

	return result;
}

/*
	Render all the geometries within the renderItemList using specified technique

	Render the items against all compatible passes of the selected technique.
*/
bool dx11ShaderNode::renderTechnique(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11EffectTechnique* dxTechnique,
									unsigned int numPasses, const MStringArray& passSem,
									const RenderItemList& renderItemList, const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const
{
	bool result = false;

	for(unsigned int passId = 0; passId < numPasses; ++passId)
	{
		dx11ShaderDX11Pass* dxPass = activatePass(dxDevice, dxContext, dxTechnique, passId, passSem, renderType);
		if(dxPass)
		{
			result |= renderPass(dxDevice, dxContext, dxPass, renderItemList, varyingParameters, renderType, indexBufferType);
		}
	}

	return result;
}

/*
	Render all the geometries within the renderItemList using specified pass
*/
bool dx11ShaderNode::renderPass(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11Pass* dxPass,
								const RenderItemList& renderItemList,
								const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const
{
	bool result = false;

	size_t numRenderItems = renderItemList.size();
	for (size_t renderItemIdx = 0; renderItemIdx < numRenderItems; ++renderItemIdx)
	{
		const MHWRender::MRenderItem* renderItem = renderItemList[renderItemIdx];
		if(renderItem)
		{
			const MHWRender::MGeometry* geometry = renderItem->geometry();
			if(geometry)
			{
				int primitiveStride;
				MHWRender::MGeometry::Primitive primitiveType = renderItem->primitive(primitiveStride);
				result |= renderPass(dxDevice, dxContext, dxPass, geometry, primitiveType, primitiveStride, varyingParameters, renderType, indexBufferType);
			}
		}
	}

	return result;
}

/*
	Render a single geometry using specified technique

	Render the geometry against all compatible passes of the selected technique.
*/
bool dx11ShaderNode::renderTechnique(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11EffectTechnique* dxTechnique, unsigned int numPasses,
									const MHWRender::MGeometry* geometry, MHWRender::MGeometry::Primitive primitiveType, unsigned int primitiveStride,
									const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const
{
	bool result = false;

	for(unsigned int passId = 0; passId < numPasses; ++passId)
	{
		dx11ShaderDX11Pass* dxPass = activatePass(dxDevice, dxContext, dxTechnique, passId, renderType);
		if(dxPass)
		{
			result |= renderPass(dxDevice, dxContext, dxPass, geometry, primitiveType, primitiveStride, varyingParameters, renderType, indexBufferType);
		}
	}

	return result;
}

/*
	Render a single geometry using specified technique to a texture target (swatch and uv editor)
*/
bool dx11ShaderNode::renderTechnique(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11EffectTechnique* dxTechnique, unsigned int numPasses,
									MHWRender::MRenderTarget* textureTarget, unsigned int width, unsigned int height, float clearColor[4],
									const MHWRender::MGeometry* geometry, MHWRender::MGeometry::Primitive primitiveType, unsigned int primitiveStride,
									const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const
{
	ID3D11RenderTargetView* textureView = (ID3D11RenderTargetView*)(textureTarget->resourceHandle());
	if(textureView == NULL)
		return false;

	if(fMayaSwatchRenderVar && (renderType == RENDER_SWATCH || renderType == RENDER_SWATCH_PROXY))
		fMayaSwatchRenderVar->AsScalar()->SetBool( true );

	ID3D11DeviceContext* dxContext = NULL;
	dxDevice->GetImmediateContext(&dxContext);

	ContextStates contextStates;
	backupStates(dxContext, contextStates);

	// Set colour and depth surfaces.
	dxContext->OMSetRenderTargets( 1, &textureView, NULL );

	// Setup viewport
	const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)(width), (float)(height), 0.0f, 1.0f };
	dxContext->RSSetViewports( 1, &viewport );

	// Clear the entire buffer (RGB, Depth)
 	dxContext->ClearRenderTargetView( textureView, clearColor );

	bool result = renderTechnique(dxDevice, dxContext, dxTechnique, numPasses,
							geometry, MHWRender::MGeometry::kTriangles, 3,
							varyingParameters, renderType, indexBufferType);

	// Clean up
	restoreStates(dxContext, contextStates);
	dxContext->Release();

	if(fMayaSwatchRenderVar && (renderType == RENDER_SWATCH || renderType == RENDER_SWATCH_PROXY))
		fMayaSwatchRenderVar->AsScalar()->SetBool( false );

	return result;
}

/*
	Render a single geometry using specified pass

	For the swatch rendering, the geometry buffers are provided by MGeometryUtilities,
	if the crack free tessellation (PNAEN9 and PNAEN18) is enabled,
	temporary buffers are created and the CrackFreePrimitiveGenerator is applied.

	To improve the rendering performance, the input layout is cached and reused as much as possible
	until the list of vertex buffers changes - usually when another technique is selected.
*/
bool dx11ShaderNode::renderPass(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11Pass* dxPass,
								const MHWRender::MGeometry* geometry, MHWRender::MGeometry::Primitive primitiveType, unsigned int primitiveStride,
								const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const
{
	unsigned int vtxBufferCount = (geometry != NULL ? geometry->vertexBufferCount() : 0);
	unsigned int idxBufferCount = (geometry != NULL ? geometry->indexBufferCount() : 0);
	if(idxBufferCount == 0 || vtxBufferCount == 0 || vtxBufferCount >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
		return false;

	bool bContainsHullShader = passHasHullShader(dxPass);

	bool bAddPNAENAdjacentEdges = false;
	bool bAddPNAENDominantEdges = false;
	bool bAddPNAENDominantPosition = false;
	std::vector<float> floatPNAENPositionBuffer;
	std::vector<float> floatPNAENUVBuffer;
	if(renderType == RENDER_SWATCH)
	{
		if(indexBufferType == "PNAEN18") {
			bAddPNAENAdjacentEdges = true;
			bAddPNAENDominantEdges = true;
			bAddPNAENDominantPosition = true;
		}
		else if (indexBufferType == "PNAEN9") {
			bAddPNAENAdjacentEdges = true;
		}
	}

	// Set up vertex buffers and input layout
	// ---------------------------------------------------------------------------
	D3D11_INPUT_ELEMENT_DESC	layout[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	ID3D11Buffer*				vtxBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];

	unsigned int				strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	unsigned int				offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	int							numBoundBuffers = 0;
	MStringArray				mappedVertexBuffers;

	for (unsigned int vtxId = 0; vtxId < vtxBufferCount; ++vtxId)
	{
		const MHWRender::MVertexBuffer* buffer = geometry->vertexBuffer(vtxId);
		if (buffer == NULL)
			continue;

		const MHWRender::MVertexBufferDescriptor& desc = buffer->descriptor();
		ID3D11Buffer* vtxBuffer = (ID3D11Buffer*)buffer->resourceHandle();
		if (vtxBuffer == NULL)
			continue;

		unsigned int					fieldOffset		= desc.offset();
		unsigned int					fieldStride		= desc.stride();
		int								dimension		= desc.dimension();
		MHWRender::MGeometry::Semantic	semantic		= desc.semantic();

#ifdef PRINT_DEBUG_INFO
		fprintf(
			stderr,
			"REQUESTED-VB: Buffer(%s)\n", MHWRender::MGeometry::semanticString(semantic).asChar()
		);
#endif

		bool isCustomSemantic = (desc.semanticName().length() > 0);

		MatchingParameters matchingParameters;
		getDstSemanticsFromSrcVertexDescriptor(varyingParameters, desc, matchingParameters);
		size_t semanticBufferCount = matchingParameters.size();

		// See if name was previously bound together with another one:
		// Probably redundant since we do not declare duplicate vertex buffers
		// in dx11ShaderNode::buildVertexDescriptorFromVaryingParameters
		if (semanticBufferCount > 1 && isCustomSemantic)
		{
			for (unsigned int iSeen = 0; iSeen < mappedVertexBuffers.length(); ++iSeen)
			{
				if (mappedVertexBuffers[iSeen] == desc.name())
				{
					semanticBufferCount = 0;
					break;
				}
			}
			mappedVertexBuffers.append(desc.name());
		}
		if(semanticBufferCount == 0)
			continue;

		if (bAddPNAENAdjacentEdges &&
			( (semantic == MHWRender::MGeometry::kPosition && floatPNAENPositionBuffer.empty()) ||
				(semantic == MHWRender::MGeometry::kTexture && floatPNAENUVBuffer.empty()) ) )
		{
			std::vector<float>& data = (semantic == MHWRender::MGeometry::kPosition ? floatPNAENPositionBuffer : floatPNAENUVBuffer);

			unsigned int size = buffer->vertexCount() * dimension;
			data.resize(size);

			MHWRender::MVertexBuffer* nonConstBuffer = const_cast<MHWRender::MVertexBuffer*>(buffer);

			const void* values = nonConstBuffer->map();
			memcpy(&data[0], values, size * sizeof(float));
			nonConstBuffer->unmap();
		}

		// We can have multiple bindings at the same input slot:
		int inputSlot = numBoundBuffers;

		// multiple buffers can be bound to the same output buffer
		// we will loop through
		// -------------------------------------------------------
		for (size_t semanticId = 0; semanticId < semanticBufferCount; ++semanticId)
		{
			const MatchingParameter& param = matchingParameters[semanticId];

			MHWRender::MGeometry::DataType vertexDataType = desc.dataType();
			switch (vertexDataType)
			{
			case MHWRender::MGeometry::kFloat:
			{
				fieldStride *= sizeof(float);
				switch (dimension) {
					case 1: layout[numBoundBuffers].Format = DXGI_FORMAT_R32_FLOAT; break;
					case 2: layout[numBoundBuffers].Format = DXGI_FORMAT_R32G32_FLOAT; break;
					case 3: layout[numBoundBuffers].Format = DXGI_FORMAT_R32G32B32_FLOAT; break;
					case 4: layout[numBoundBuffers].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
					default: continue;
				}
				break;
			}
			case MHWRender::MGeometry::kInt32:
			case MHWRender::MGeometry::kUnsignedInt32:
			{
				fieldStride *= sizeof(int);
				switch (dimension) {
					case 1: layout[numBoundBuffers].Format = DXGI_FORMAT_R32_UINT; break;
					case 2: layout[numBoundBuffers].Format = DXGI_FORMAT_R32G32_UINT; break;
					case 3: layout[numBoundBuffers].Format = DXGI_FORMAT_R32G32B32_UINT; break;
					case 4: layout[numBoundBuffers].Format = DXGI_FORMAT_R32G32B32A32_UINT; break;
					default: continue;
				}
				break;
			}
			default:
				continue;
			}

			ID3D11Buffer* customVtxBuffer = vtxBuffer;
			unsigned int customFieldOffset = fieldOffset;
			unsigned int customFieldStride = fieldStride;

			if (isCustomSemantic)
			{
				// we just use the semantic name if there is one
				layout[numBoundBuffers].SemanticName = desc.semanticName().asChar();
				layout[numBoundBuffers].SemanticIndex = 0;

				// it's a custom semantic, that is probably managed by a vertex buffer generator
				// if geometry dimension or type do not match varying parameter create an empty buffer
				int elementSize = desc.dataTypeSize();
				if(dimension != param.dimension || elementSize != param.elementSize)
				{
					int bufferSize = elementSize * dimension * buffer->vertexCount();
					char *bufferData = new char[bufferSize];
					::memset(bufferData, bufferSize, 0);

					// Create new vertex buffer
					const D3D11_BUFFER_DESC bufDesc = { bufferSize, D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
					const D3D11_SUBRESOURCE_DATA bufData = { bufferData, 0, 0 };
					customVtxBuffer = NULL;
					dxDevice->CreateBuffer(&bufDesc, &bufData, &customVtxBuffer);
					delete [] bufferData;
				}
			}
			else
			{
				semantic = param.semantic;
				int semanticIndex = param.semanticIndex;
				switch (semantic) {
					case MHWRender::MGeometry::kPosition:	layout[numBoundBuffers].SemanticName = "POSITION"; break;
					case MHWRender::MGeometry::kNormal:		layout[numBoundBuffers].SemanticName = "NORMAL"; break;
					case MHWRender::MGeometry::kTexture:	layout[numBoundBuffers].SemanticName = "TEXCOORD"; break;
					case MHWRender::MGeometry::kColor:		layout[numBoundBuffers].SemanticName = "COLOR"; break;
					case MHWRender::MGeometry::kTangent:	layout[numBoundBuffers].SemanticName = "TANGENT"; break;
					case MHWRender::MGeometry::kBitangent:	layout[numBoundBuffers].SemanticName = "BINORMAL"; break;
					default: continue;
				}
				layout[numBoundBuffers].SemanticIndex = semanticIndex;
			}

			if(customVtxBuffer)
			{
#ifdef PRINT_DEBUG_INFO
				fprintf(
					stderr,
					"VTX_BUFFER_INFO: Buffer(%d), Name(%s), BufferType(%s), BufferDimension(%d), BufferSemantic(%s), Offset(%d), Stride(%d), Handle(%p)\n",
					vtxId,
					desc.name().asChar(),
					MHWRender::MGeometry::dataTypeString(vertexDataType).asChar(),
					dimension,
					MHWRender::MGeometry::semanticString(semantic).asChar(),
					fieldOffset,
					fieldStride,
					vtxBuffer);
#endif

				layout[numBoundBuffers].InputSlot = inputSlot;
				layout[numBoundBuffers].AlignedByteOffset = 0;
				layout[numBoundBuffers].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
				layout[numBoundBuffers].InstanceDataStepRate = 0;
				vtxBuffers[numBoundBuffers] = customVtxBuffer;
				strides[numBoundBuffers] = customFieldStride;
				offsets[numBoundBuffers] = customFieldOffset;

				if(customVtxBuffer != vtxBuffer)
					customVtxBuffer->Release();

				++numBoundBuffers;
			}
		}
	}
	// Activate vertex buffers
	if (numBoundBuffers <= 0) return false;
	dxContext->IASetVertexBuffers(0, numBoundBuffers, vtxBuffers, strides, offsets);

	// Acquire and set input layout based on vertex buffers
	ID3D11InputLayout* inputLayout = getInputLayout(dxDevice, dxPass, numBoundBuffers, layout);
	if (inputLayout == NULL) return false;
	dxContext->IASetInputLayout(inputLayout);

	bool result = false;

	// Setup index buffers and draw
	for (unsigned int idxId = 0; idxId < idxBufferCount; ++idxId)
	{
		const MHWRender::MIndexBuffer* buffer = geometry->indexBuffer(idxId);
		if (buffer == NULL)
			continue;

		ID3D11Buffer* idxBuffer	= (ID3D11Buffer*)buffer->resourceHandle();
		if (idxBuffer == NULL)
			continue;

		MHWRender::MGeometry::DataType indexDataType = buffer->dataType();

		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		unsigned int formatSize = 0;
		switch (indexDataType)
		{
		case MHWRender::MGeometry::kChar:
		case MHWRender::MGeometry::kUnsignedChar:
			format = DXGI_FORMAT_R8_UINT;
			formatSize = 1;
			break;
		case MHWRender::MGeometry::kInt16:
		case MHWRender::MGeometry::kUnsignedInt16:
			format = DXGI_FORMAT_R16_UINT;
			formatSize = 2;
			break;
		case MHWRender::MGeometry::kInt32:
		case MHWRender::MGeometry::kUnsignedInt32:
			format = DXGI_FORMAT_R32_UINT;
			formatSize = 4;
			break;
		default:
			continue;
		}

		unsigned int indexBufferSize = buffer->size();

		ID3D11Buffer* customIdxBuffer = idxBuffer;
		if (bAddPNAENAdjacentEdges && floatPNAENPositionBuffer.empty() == false && floatPNAENUVBuffer.empty() == false && formatSize != 2)
		{
			unsigned int indexCount = indexBufferSize;

			MUintArray currentIndexBuffer;
			currentIndexBuffer.setLength(indexCount);

			MHWRender::MIndexBuffer* nonConstBuffer = const_cast<MHWRender::MIndexBuffer*>(buffer);

			void* indices = nonConstBuffer->map();
			for (unsigned int iidx = 0; iidx < indexCount; ++iidx)
			{
				switch (indexDataType)
				{
					case MHWRender::MGeometry::kChar:			currentIndexBuffer[iidx] = (unsigned int)((__int8*)indices)[iidx]; break;
					case MHWRender::MGeometry::kUnsignedChar:	currentIndexBuffer[iidx] = (unsigned int)((unsigned __int8*)indices)[iidx]; break;
					case MHWRender::MGeometry::kInt16:			currentIndexBuffer[iidx] = (unsigned int)((__int16*)indices)[iidx]; break;
					case MHWRender::MGeometry::kUnsignedInt16:	currentIndexBuffer[iidx] = (unsigned int)((unsigned __int16*)indices)[iidx]; break;
					case MHWRender::MGeometry::kInt32:			currentIndexBuffer[iidx] = (unsigned int)((__int32*)indices)[iidx]; break;
					case MHWRender::MGeometry::kUnsignedInt32:	currentIndexBuffer[iidx] = (unsigned int)((unsigned __int32*)indices)[iidx]; break;
					default:	continue;
				}
			}
			nonConstBuffer->unmap();

			unsigned int numTri = indexCount/3;
			unsigned int triSize = CrackFreePrimitiveGenerator::computeTriangleSize(bAddPNAENAdjacentEdges, bAddPNAENDominantEdges, bAddPNAENDominantPosition);
			indexBufferSize = numTri * triSize;
			unsigned int dataBufferSize = indexBufferSize * formatSize;
			indices = new char[dataBufferSize];
			CrackFreePrimitiveGenerator::mutateIndexBuffer( currentIndexBuffer, &floatPNAENPositionBuffer[0], &floatPNAENUVBuffer[0],
								bAddPNAENAdjacentEdges, bAddPNAENDominantEdges, bAddPNAENDominantPosition,
								(formatSize == 1 ? MHWRender::MGeometry::kUnsignedChar : MHWRender::MGeometry::kUnsignedInt32), indices );

			primitiveStride = triSize;
			primitiveType = MHWRender::MGeometry::kPatch;

			// Create new index buffer
			const D3D11_BUFFER_DESC bufDesc = { dataBufferSize, D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
			const D3D11_SUBRESOURCE_DATA bufData = { indices, 0, 0 };
			customIdxBuffer = NULL;
			dxDevice->CreateBuffer(&bufDesc, &bufData, &customIdxBuffer);
			delete [] indices;
		}

		if (customIdxBuffer)
		{
			D3D11_PRIMITIVE_TOPOLOGY topo = getPrimitiveTopology(primitiveType, primitiveStride, bContainsHullShader);
			if(topo == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED) continue;

#ifdef PRINT_DEBUG_INFO
			fprintf(stderr, "IDX_BUFFER_INFO: Buffer(%d), IndexingPrimType(%s), IndexType(%s), IndexCount(%d), Handle(%p)\n",
				idxId,
				MHWRender::MGeometry::primitiveString(primitiveType).asChar(),
				MHWRender::MGeometry::dataTypeString(indexDataType).asChar(),
				indexBufferSize,
				customIdxBuffer);
#endif

			// Activate index buffer and draw
			dxContext->IASetIndexBuffer(customIdxBuffer, format, 0);
			dxContext->IASetPrimitiveTopology(topo);
			dxContext->DrawIndexed(indexBufferSize, 0, 0);

			result |= true; // drew something

			if(customIdxBuffer != idxBuffer)
				customIdxBuffer->Release();
		}
	}

	return result;
}

/*
	Backup all states of dx context, should called before each render operation
*/
void dx11ShaderNode::backupStates(dx11ShaderDX11DeviceContext *dxContext, ContextStates &states) const
{
	dxContext->RSGetState(&(states.rasterizerState));
	dxContext->OMGetDepthStencilState(&(states.depthStencilState), &(states.stencilRef));
	dxContext->OMGetBlendState(&(states.blendState), states.blendFactor, &(states.sampleMask));
}

/*
	Restore all states of dx context, should called after each render operation
*/
void dx11ShaderNode::restoreStates(dx11ShaderDX11DeviceContext *dxContext, ContextStates &states) const
{
	if(states.rasterizerState) {
		dxContext->RSSetState(states.rasterizerState);
		states.rasterizerState->Release();
		states.rasterizerState = NULL;
	}

	if(states.depthStencilState) {
		dxContext->OMSetDepthStencilState(states.depthStencilState, states.stencilRef);
		states.depthStencilState->Release();
		states.depthStencilState = NULL;
	}

	if(states.blendState) {
		dxContext->OMSetBlendState(states.blendState, states.blendFactor, states.sampleMask);
		states.blendState->Release();
		states.blendState = NULL;
	}
}

/*
	Update any parameters on shader
*/
bool dx11ShaderNode::updateParameters( const MHWRender::MDrawContext& context, MUniformParameterList& uniformParameters, ResourceTextureMap &resourceTexture, ERenderType renderType ) const
{
	// If the render frame stamp did not change, it's likely that this shader is used by multiple objects,
	// and is called more than once in a single frame render.
	// No need to update the light parameters (again) as it's quite costly
	bool updateLightParameters = true;
	bool updateViewParams = false;
	bool updateTextures = fForceUpdateTexture;
	if(renderType == RENDER_SCENE)
	{
		// We are rendering the scene
		MUint64 currentFrameStamp = context.getFrameStamp();
		updateLightParameters = (currentFrameStamp != fLastFrameStamp);
		updateViewParams = (currentFrameStamp != fLastFrameStamp);
		fLastFrameStamp = currentFrameStamp;
		fForceUpdateTexture = false;
	}
	else if(renderType == RENDER_SWATCH)
	{
		// We are rendering the swatch using current effect
		// Reset the renderId, to be sure that the next updateParameters() will go through
		fLastFrameStamp = (MUint64)-1;
		fForceUpdateTexture = false;
	}
	else
	{
		// We are rendering the proxy swatch or the uv texture (temporary effect)
		fLastFrameStamp = (MUint64)-1;
		updateLightParameters = false;
		// We need to update the texture when rendering the swatch or uv texture using a custom effect
		updateTextures = true;
	}

	/*
	All parameters that are driven by a light and require an update first get
	refreshed using the value stored in the uniform parameter. This helper
	function will populate the set of parameters that need to be reset.

	The set will contain all light parameters that a part of a light group
	that was marked as dirty either because a value changed, or because the
	lighting sources have changed (like when going from swatch render back
	to scene render).
	*/
	std::set<int> lightParametersToUpdate;
	if(updateLightParameters)
	{
		getLightParametersToUpdate(lightParametersToUpdate, renderType);
	}

	// Update parameters that are driven by Global Viewport parameters, like full viewport Gamma Correction:
	if (updateViewParams)
	{
		updateViewportGlobalParameters( context );
	}

	// Update uniform values
	// -------------------------------------
	D3DX11_EFFECT_TYPE_DESC descType;
	for( int u = uniformParameters.length(); u--; ) {
		MUniformParameter uniform = uniformParameters.getElement(u);

		if( uniform.hasChanged(context) || lightParametersToUpdate.count(u) || (updateTextures && uniform.isATexture()) ) {

			ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
			if (!effectVariable)  break;

			effectVariable->GetType()->GetDesc(&descType);

			switch( uniform.type()) {
				case MUniformParameter::kTypeFloat: {

					const float* data = uniform.getAsFloatArray(context);
					if (data) {
						if (descType.Class == D3D10_SVC_SCALAR) {
							effectVariable->AsScalar()->SetFloat( data[0] );
						} else if (descType.Class == D3D10_SVC_VECTOR) {
							effectVariable->AsVector()->SetFloatVector( (float*)data );
						} else if (descType.Class == D3D10_SVC_MATRIX_COLUMNS) {
							effectVariable->AsMatrix()->SetMatrix( (float*)data );
						} else if (descType.Class == D3D10_SVC_MATRIX_ROWS) {
							effectVariable->AsMatrix()->SetMatrixTranspose( (float*)data );
						} else {
							// @@@@@ Error ?!?!
						}
					}
				} break;
				case MUniformParameter::kTypeInt:
				case MUniformParameter::kTypeEnum:
					{
					if (descType.Class == D3D10_SVC_SCALAR) {
						effectVariable->AsScalar()->SetInt( uniform.getAsInt(context) );
					} else {
						// @@@@@ Error ?!?!
					}
				} break;
				case MUniformParameter::kTypeBool: {
					if (descType.Class == D3D10_SVC_SCALAR) {
						effectVariable->AsScalar()->SetBool( uniform.getAsBool(context) );
					} else {
						// @@@@@ Error ?!?!
					}
				} break;
				case MUniformParameter::kTypeString: {
					// @@@@@ Error ?!?!
				} break;
				default: {
					if( uniform.isATexture()) {
						ID3DX11EffectShaderResourceVariable* resourceVar = effectVariable->AsShaderResource();
						if (resourceVar) {
							MUniformParameter::DataSemantic sem = uniform.semantic();
							if (sem == MUniformParameter::kSemanticTranspDepthTexture) {
								const MHWRender::MTexture *tex = context.getInternalTexture(
									MHWRender::MDrawContext::kDepthPeelingTranspDepthTexture);
								resourceVar->SetResource((ID3D11ShaderResourceView*)tex->resourceHandle());
							}
							else if (sem == MUniformParameter::kSemanticOpaqueDepthTexture) {
								const MHWRender::MTexture *tex = context.getInternalTexture(
									MHWRender::MDrawContext::kDepthPeelingOpaqueDepthTexture);
								resourceVar->SetResource((ID3D11ShaderResourceView*)tex->resourceHandle());
							} else {
								MString textureName, layerName;
								int alphaChannelIdx;
								getTextureDesc(context, uniform, textureName, layerName, alphaChannelIdx);
								assignTexture(resourceVar, textureName, layerName, alphaChannelIdx, resourceTexture);
							}
						}
					}
				} break;
			}
		}
	}

	if(updateLightParameters)
	{
		// Update using draw context properties if light is explicitely connected.
		// Must be done after we have reset lights to their previous values as
		// explicit light connections overrides values stored in shader:
		updateExplicitLightConnections(context, renderType);

		updateImplicitLightConnections(context, renderType);
	}

	return true;
}

void dx11ShaderNode::updateViewportGlobalParameters( const MHWRender::MDrawContext& context ) const
{
	if(fMayaGammaCorrectVar)
	{
		bool isGammaEnabled = context.getPostEffectEnabled( MHWRender::MFrameContext::kGammaCorrection );
		fMayaGammaCorrectVar->AsScalar()->SetBool( isGammaEnabled );
	}
}

/*
The effect needs to compute shadows only if three conditions are met:
	1- Shadowcasting is globally enabled (Lighting->Shadow in view)
	2- The surface is illuminated by a light that creates shadows
	3- The surface is marked as receiving shadows
The first two conditions were detected and set in the effect when we
last refreshed the light parameters, but the last one depends on the
geometry being rendered and can change from one object to another one
in the draw list.

Here we store the parameter position of all attributes of "ShadowOn"
semantics along with the current value stored in the shader.
*/
void dx11ShaderNode::initShadowFlagBackupState(TshadowFlagBackupState& stateBackup ) const
{
	if (stateBackup.empty()) {
		// Build backup state with all current values of parameters with ShadowOn semantics:
		for(size_t shaderLightIndex = 0; shaderLightIndex < fLightParameters.size(); ++shaderLightIndex )
		{
			const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];

			LightParameterInfo::TConnectableParameters::const_iterator it = shaderLightInfo.fConnectableParameters.begin();
			LightParameterInfo::TConnectableParameters::const_iterator itEnd = shaderLightInfo.fConnectableParameters.end();
			for (; it != itEnd; ++it)
			{
				const int parameterType  = it->second;
				if (parameterType == CUniformParameterBuilder::eLightShadowOn)
				{
					const int parameterIndex = it->first;
					MUniformParameter uniform = fUniformParameters.getElement(parameterIndex);
					ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
					if (effectVariable) {
#if _MSC_VER < 1700
						BOOL currentState;
#else
						bool currentState;
#endif
						effectVariable->AsScalar()->GetBool( &currentState );
						stateBackup.insert(TshadowFlagBackupState::value_type(parameterIndex, currentState != 0));
					}
				}
			}
		}
	}
}

/*
Here we adjust the "ShadowON" parameter of all shader light groups
to take into account the "ReceivesShadow" state of the geometry
about to be rendered.
*/
void dx11ShaderNode::setPerGeometryShadowOnFlag(bool receivesShadows, TshadowFlagBackupState& stateBackup ) const
{
	// Set the state of all ShadowOn parameters:
	TshadowFlagBackupState::const_iterator it = stateBackup.begin();
	TshadowFlagBackupState::const_iterator itEnd = stateBackup.end();
	for (; it != itEnd; ++it)
	{
		setParameterAsScalar(it->first, it->second && receivesShadows);
	}
}

// ***********************************
// Uniform and varying parameters
// ***********************************

void dx11ShaderNode::clearParameters()
{
	clearLightConnectionData();

	fUniformParameters.setLength(0);
	setUniformParameters( fUniformParameters, true );

	fVaryingParametersUpdateId = 0;
	fVaryingParameters.setLength(0);
	setVaryingParameters( fVaryingParameters, true );

	fTechniqueIsTransparent = eOpaque;
	fOpacityPlugName = "";
	fTransparencyTestProcName = "";

	initMayaParameters();
}

/*
	Checks the shader for any parameters that affect Uniform Parameter creation.
*/
void dx11ShaderNode::preBuildUniformParameterList()
{
	if (!fEffect || !fTechnique) {
		return;
	}

	// does the shader want us to use the variable name as maya attribute name (instead of UI name)?
	fVariableNameAsAttributeName = true;
	getAnnotation(fTechnique, dx11ShaderAnnotation::kVariableNameAsAttributeName, fVariableNameAsAttributeName);
}

/*
Build the uniform parameter list by processing all the parameter information
stored in the effect. We let the CUniformParameterBuilder helper class take
care of extracting all the information, then we sort all parameters according
to a potentially defined UIOrder. This is also where we properly order the
UIGroups defined in the parameter annotations, and create the helper structure
that remembers which parameter belongs to which group.
*/
bool dx11ShaderNode::buildUniformParameterList()
{
	preBuildUniformParameterList();

	setTopoDirty();

	// Get the effect description.
	if (!fEffect) {
		return false;
	}

    D3DX11_EFFECT_DESC		desc;
    fEffect->GetDesc(&desc);

    // Iterate the effect parameters, processing each one.
	// --------------------------------------------------------------
	fUniformParameters.setLength(0);
	std::vector< CUniformParameterBuilder > builders;
	std::vector< CUniformParameterBuilder* > successfulBuilder;
	builders.resize(desc.GlobalVariables);
	for (unsigned int i = 0; i < desc.GlobalVariables; i++)
	{
        ID3DX11EffectVariable* pD3DVar = fEffect->GetVariableByIndex(i);
		CUniformParameterBuilder& builder = builders[i];
		builder.init(pD3DVar,this,i);
		if(builder.build())
		{
			successfulBuilder.push_back(&builder);
		}
		else
		{
			fWarningLog += builder.getWarnings();
		}
    }
	std::sort(successfulBuilder.begin(),successfulBuilder.end(),CUniformParameterBuilder::compareUIOrder );

	fUIGroupParameters.clear();
	fUIGroupParameters.resize(fUIGroupNames.length());

	// All the groups were initially added in the same order as
	// they were returned from the compiler. We want them to be
	// sorted by UIOrder instead:
	std::vector<int> uiGroupRemapping;
	uiGroupRemapping.resize(fUIGroupNames.length(), -1);
	int numRemapped = 0;
	MStringArray sortedUIGroupNames;

	std::vector< CUniformParameterBuilder* >::iterator iter = successfulBuilder.begin();
	for(; iter != successfulBuilder.end();++iter)
	{
		CUniformParameterBuilder* pBuilder(*iter);
		fUniformParameters.append(pBuilder->getParameter());

		int uiGroupIndex = pBuilder->getUIGroupIndex();
		if (uiGroupIndex >= 0)
		{
			if (uiGroupRemapping[uiGroupIndex] == -1) {
				sortedUIGroupNames.append(fUIGroupNames[(unsigned int)uiGroupIndex]);
				uiGroupRemapping[uiGroupIndex] = numRemapped;
				++numRemapped;
			}
			uiGroupIndex = uiGroupRemapping[uiGroupIndex];

			fUIGroupParameters[uiGroupIndex].push_back(fUniformParameters.length() - 1);
		}
	}
	fUIGroupNames = sortedUIGroupNames;

	updateImplicitLightParameterCache( successfulBuilder);
	displayErrorAndWarnings();
	return true;
}

/*
	Parse through the current technique.

	For the current technique parse through the passes and for each pass extract out the required layout
	to use at draw time.

	For all pass on a technique we need the combined format to be returned as the vertex requirement.
	We can keep a set of MVertexBufferDescriptors for this (one per technique).

	The varying parameters are used by Maya to produce the required vertex buffers,
	and during the rendering to activate the necessary buffers and build the input layout
	that matches the current pass being processed.
*/
bool dx11ShaderNode::buildVaryingParameterList()
{
	if (!fEffect) {
		return false;
	}

	fVaryingParametersUpdateId = 0;
	fVaryingParameters.setLength(0);

    ID3DX11EffectTechnique* dxTechnique = fEffect->GetTechniqueByIndex( fTechniqueIdx );
	if (dxTechnique)
	{
	    D3DX11_TECHNIQUE_DESC descTechnique;
		dxTechnique->GetDesc(&descTechnique);

		::buildVaryingParameterList(dxTechnique, descTechnique.Passes, fVaryingParameters, fErrorLog, fWarningLog, fTechniqueIndexBufferType);
	}

	buildVertexDescriptorFromVaryingParameters();
	displayErrorAndWarnings();
	return true;
}

bool  dx11ShaderNode::buildVertexDescriptorFromVaryingParameters()
{
	fVaryingParametersVertexDescriptorList.clear();

	for (int i=0; i<fVaryingParameters.length(); i++) {
		MVaryingParameter varying = fVaryingParameters.getElement(i);

		// We need to find all the vertexbuffer requirements
		// they are based on the source of the Varying parameter
		// i.e We will have to use the right source to set in the right
		// final shader destination
		// -----------------------------------------------------
		MHWRender::MGeometry::Semantic	sourceSemantic	= getVertexBufferSemantic(varying.getSourceType());
        MString	semanticName = varying.semanticName();

		// generate the right name for the binding of the vertex buffer to the righ data source
		// based on the data adjust to the right source
		// -------------------------------------------------------------------------------------
		MString	sourceSetName	= varying.getSourceSetName();
        MHWRender::MVertexBufferDescriptor desc(
            sourceSetName,
            sourceSemantic,
            (MHWRender::MGeometry::DataType) varying.type(),
            varying.dimension());
        desc.setSemanticName(semanticName);

		// Do not create extra vertex buffers if we have multiple UV
		// that can be mapped to the same one:
		bool addDescriptor = true;
		for (int iVB=0; iVB < fVaryingParametersVertexDescriptorList.length(); ++iVB) {
			MHWRender::MVertexBufferDescriptor existingDesc;
			fVaryingParametersVertexDescriptorList.getDescriptor(iVB, existingDesc);
			if (existingDesc.name() == desc.name() &&
				existingDesc.semantic() == desc.semantic() &&
				existingDesc.semanticName() == desc.semanticName() &&
				existingDesc.dataType() == desc.dataType() &&
				existingDesc.dimension() == desc.dimension() &&
				existingDesc.offset() == desc.offset() &&
				existingDesc.stride() == desc.stride() )
			{
				addDescriptor = false;
				break;
			}
		}
		if (addDescriptor)
			fVaryingParametersVertexDescriptorList.append(desc);

#ifdef PRINT_DEBUG_INFO
		fprintf(
			stderr,
			"PrepareVertexBuffer: Name(%s), SourceSemantics(%s)\n",
			sourceSetName.asChar(),
			MHWRender::MGeometry::semanticString(sourceSemantic).asChar()
		);
#endif
	}
	return true;
}

/*
	Identify uniform parameters with special semantic that are used for internal purpose:

	kBBoxExtraScale - controls the Maya internal bounding box extra scale.
		The parameter, visible in the Attribute Editor, will allow the user to increase the bounding box so that Maya
		will not clip the geometry when any displacement is set in the shader.
	kMayaSwatchRender - boolean parameter that can be used to identify if the shader is executed to render the swatch.
		The variable is set to true during the swatch render operation, and set to false once finished.
		The shader can then behave differently when rendering the scene and the swatch.
		This is currently used by the MayaUberShader to disable any displacement for the swatch.
*/
void dx11ShaderNode::initMayaParameters()
{
	// Find the Bounding Box Extra Scale parameter
	// It's determined by the float parameter with semantic BoundingBoxExtraScale or an annotation BoundingBoxExtraScale set to True
	bool foundBBoxExtraScale = false;
	MString bboxExtraScalePlugName;
	float bboxExtraScaleValueFromEffect = 0.0f;

	// Find the Maya Swatch Render parameter
	// It's determined by the bool parameter with semantic MayaSwatchRender
	bool foundMayaSwatchRender = false;
	fMayaSwatchRenderVar = NULL;

	// Find the Maya viewport Gamma Correction parameter
	bool foundMayaGammaCorrect = false;
	fMayaGammaCorrectVar = NULL;

	// Find any shader parameters that may change the geo of the object on hardware
	fShaderChangesGeo = false;

	unsigned int paramCount = fUniformParameters.length();
	for( unsigned int i = 0; i < paramCount; ++i )
	{
		MUniformParameter param = fUniformParameters.getElement(i);

		// look for BBoxExtraScale -- filter for float1 parameters
		if( foundBBoxExtraScale == false && param.type() == MUniformParameter::kTypeFloat && param.numElements() == 1)
		{
			ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)param.userData();
			if(effectVariable)
			{
				D3DX11_EFFECT_VARIABLE_DESC varDesc;
				effectVariable->GetDesc(&varDesc);

				// Check semantic first
				foundBBoxExtraScale = ( varDesc.Semantic != NULL && ::_stricmp(dx11ShaderSemantic::kBboxExtraScale, varDesc.Semantic) == 0 );
				if(foundBBoxExtraScale == false)
				{
					// Then check annotation
					bool boolValue = 0;
					foundBBoxExtraScale = (getAnnotation(effectVariable, dx11ShaderSemantic::kBboxExtraScale, boolValue) && boolValue);
				}

				if(foundBBoxExtraScale)
				{
					bboxExtraScalePlugName = param.getPlug().name();	// get the plug name, might be different than the variable name
					effectVariable->AsScalar()->GetFloat( &bboxExtraScaleValueFromEffect );
					continue;
				}
			}
		}

		// look for MayaSwatchRender or MayaGammaCorrection -- filter for bool parameters
		if( (foundMayaSwatchRender == false || foundMayaGammaCorrect == false) && param.type() == MUniformParameter::kTypeBool && param.numElements() == 1)
		{
			ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)param.userData();
			if(effectVariable)
			{
				D3DX11_EFFECT_VARIABLE_DESC varDesc;
				effectVariable->GetDesc(&varDesc);

				foundMayaSwatchRender = ( varDesc.Semantic != NULL && ::_stricmp(dx11ShaderSemantic::kMayaSwatchRender, varDesc.Semantic) == 0 );
				if(foundMayaSwatchRender)
				{
					fMayaSwatchRenderVar = effectVariable;
					fMayaSwatchRenderVar->AsScalar()->SetBool( false );	// reset to false by default
					param.setUIHidden(true);	// hide from UI
					continue;
				}

				foundMayaGammaCorrect = ( varDesc.Semantic != NULL && ::_stricmp(dx11ShaderSemantic::kMayaGammaCorrection, varDesc.Semantic) == 0 );
				if(foundMayaGammaCorrect)
				{
					fMayaGammaCorrectVar = effectVariable;
					fMayaGammaCorrectVar->AsScalar()->SetBool( false );	// reset to false by default
					param.setUIHidden(true);	// hide from UI
					continue;
				}
			}
		}

		// search parameters to see if anything is used in the shader that causes the shader to change the geo on hardware
		if ( fShaderChangesGeo == false && param.type() == MUniformParameter::kTypeFloat && param.numElements() == 1 )
		{
			ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)param.userData();
			if(effectVariable)
			{
				D3DX11_EFFECT_VARIABLE_DESC varDesc;
				effectVariable->GetDesc(&varDesc);

				// Is the 'Time' semantic used?
				// This may not always mean the geo is changed by the shader, but it is possible, so to be safe we flag the shader as changing geo
				fShaderChangesGeo = (	varDesc.Semantic != NULL &&
										( ::_stricmp(dx11ShaderSemantic::kTime, varDesc.Semantic) == 0 ||
										  ::_stricmp(dx11ShaderSemantic::kAnimationTime, varDesc.Semantic) == 0 ||
										  ::_stricmp(dx11ShaderSemantic::kFrameNumber, varDesc.Semantic) == 0 ||
										  ::_stricmp(dx11ShaderSemantic::kFrame, varDesc.Semantic) == 0)
									);
			}
		}

		// early exit since we handled all known cases:
		if(foundBBoxExtraScale && foundMayaSwatchRender && foundMayaGammaCorrect && fShaderChangesGeo)
			break;
	}

	if(bboxExtraScalePlugName.length() == 0)
	{
		// Parameter not found, reset value
		fBBoxExtraScalePlugName.clear();
		fBBoxExtraScaleValue = 0.0f;
		return;
	}

	fBBoxExtraScalePlugName = bboxExtraScalePlugName;

	// Get value from effect only when current is invalid,
	// We don't want to overwrite the value on reload.
	if(fBBoxExtraScaleValue < 1.0f)
		fBBoxExtraScaleValue = bboxExtraScaleValueFromEffect;
}

// ***********************************
// Attibute Editor
// ***********************************

MStringArray dx11ShaderNode::getUIGroupParameters(int uiGroupIndex) const
{
	MStringArray retVal;
	if(uiGroupIndex < (int)fUIGroupParameters.size())
	{
		const std::vector<int> &groupParams(fUIGroupParameters[uiGroupIndex]);

		for (size_t iParam=0; iParam < groupParams.size(); ++iParam)
		{
			appendParameterNameIfVisible(groupParams[iParam], retVal);
		}
	}
	return retVal;
}

/*
	Find out which index corresponds to a given a UI group name.

	This function is used first by the CUniformParameterBuilder to
	get the initial index for each distinct UI group, then, after the
	names are properly sorted in dx11ShaderNode::buildUniformParameterList
	we use this function to help the dx11Shader command parse out the
	UI group names.
*/
int dx11ShaderNode::getIndexForUIGroupName(const MString& uiGroupName, bool appendGroup) {
	unsigned int index = 0;
	// Linear search in array is efficient for sizes less than 20.
	for ( ; index < fUIGroupNames.length(); index++)
		if ( fUIGroupNames[index] == uiGroupName || sanitizeName(fUIGroupNames[index]) == uiGroupName)
			return index;
	if (appendGroup)
	{
		fUIGroupNames.append(uiGroupName);
		return index;
	}
	return -1;
}

/*
	Helper function used by the AE via the dx11Shader command to
	know which light is currently driving a light group. For
	explicitly connected lights, we follow the connection to the
	light shape. For implicit lights, we check if we have a cached
	light in the light info structure.
*/
MString dx11ShaderNode::getLightConnectionInfo(int lightIndex)
{
	if(lightIndex < (int)fLightParameters.size())
	{
		LightParameterInfo& currLight = fLightParameters[lightIndex];

		MFnDependencyNode thisDependNode;
		thisDependNode.setObject(thisMObject());
		MPlug thisLightConnectionPlug = thisDependNode.findPlug(currLight.fAttrConnectedLight, true);
		if (thisLightConnectionPlug.isConnected())
		{
			// Find the light connected as source to this plug:
			MPlugArray srcCnxArray;
			thisLightConnectionPlug.connectedTo(srcCnxArray,true,false);
			if (srcCnxArray.length() > 0)
			{
				MPlug sourcePlug = srcCnxArray[0];
				MDagPath sourcePath;
				MDagPath::getAPathTo(sourcePlug.node(), sourcePath);
				MFnDependencyNode sourceTransform;
				sourceTransform.setObject(sourcePath.transform());
				return sourceTransform.name();
			}
		}

		// If light is currently cached, also return it:
		MPlug useImplicitPlug = thisDependNode.findPlug( currLight.fAttrUseImplicit, false );
		if( !useImplicitPlug.isNull() ) {
			bool useImplicit;
			useImplicitPlug.getValue( useImplicit );
			if (useImplicit)
			{
				// Make sure cached light is still in model:
				if (!currLight.fCachedImplicitLight.isNull())
				{
					MStatus status;
					MFnDagNode lightDagNode(currLight.fCachedImplicitLight, &status);
					if (status.statusCode() == MStatus::kSuccess && lightDagNode.inModel() ) {
						MDagPath cachedPath;
						MDagPath::getAPathTo(currLight.fCachedImplicitLight, cachedPath);
						MFnDependencyNode cachedTransform;
						cachedTransform.setObject(cachedPath.transform());
						return cachedTransform.name();
					}
				}
				else if (lightIndex == fImplicitAmbientLight)
					return dx11ShaderStrings::getString( dx11ShaderStrings::kAmbient );
			}
		}
	}
	return "";
}

MStringArray dx11ShaderNode::getLightableParameters(int lightIndex, bool showSemantics)
{
	MStringArray retVal;
	if(lightIndex < (int)fLightParameters.size())
	{
		LightParameterInfo& currLight = fLightParameters[lightIndex];
		for (LightParameterInfo::TConnectableParameters::const_iterator idxIter=currLight.fConnectableParameters.begin();
			idxIter != currLight.fConnectableParameters.end();
			++idxIter)
		{
			bool appended = appendParameterNameIfVisible((*idxIter).first, retVal);

			if (appended && showSemantics) {
				int paramType((*idxIter).second);
				retVal.append(CUniformParameterBuilder::getLightParameterSemantic(paramType));
			}
		}
	}
	return retVal;
}

/*
	Find out which index corresponds to a given a light group name.

	This function is used first by the CUniformParameterBuilder to
	get the initial index for each distinct light group, then, after the
	names are properly sorted in dx11ShaderNode::buildUniformParameterList
	we use this function to help the dx11Shader command parse out the
	light group names.
*/
int dx11ShaderNode::getIndexForLightName(const MString& lightName, bool appendLight) {
	unsigned int index = 0;
	// Linear search in array is efficient for sizes less than 20.
	for ( ; index < fLightNames.length(); index++)
		if ( fLightNames[index] == lightName || sanitizeName(fLightNames[index]) == lightName)
			return index;
	if (appendLight)
	{
		fLightNames.append(lightName);
		return index;
	}
	return -1;
}

/*
	In the AE we only want to expose visible parameters, so
	test here for parameter visibility:
*/
bool dx11ShaderNode::appendParameterNameIfVisible(int paramIndex, MStringArray& paramArray) const
{
	MUniformParameter uniform = fUniformParameters.getElement(paramIndex);

	MPlug uniformPlug(uniform.getPlug());
	if (uniformPlug.isNull())
		return false;

	MFnAttribute uniformAttribute(uniformPlug.attribute());
	if (uniformAttribute.isHidden())
		return false;

	paramArray.append(uniformAttribute.shortName());
	return true;
}

// ***********************************
// Light Management
// ***********************************

/* ======================================================================

	How to define dx11Shader lights:
	===============================

	When parsing the effect parameters, we search for information on
	parameters that can be grouped together as a logical light for the
	effect. This can be explicitly done by using the same light name
	string in an "Object" annotation set on all parameters that should be
	grouped together, but we also try to implicitly do this by finding a
	common name prefix on attributes that have light semantics but no
	explicit Object annotation. For the complete list of light semantics
	and annotations, please consult the SDK documentation.

	Some pointers on how dx11Shader lighting works:
	==============================================

	The lighting code in a dx11Shader can compute shadows using the shadowmap
	generated by a light. Since this shadowmap is computed while drawing, it
	can only be accessed in the light information provided by the draw context,
	which is also the best place to look if we want to know if a light is both
	visible and enabled. This is why lighting in dx11Shaders does not explicitly
	connect on light shapes and transforms to pull values, but rather matches
	scene lights with draw context lights at render time and transfers light
	parameter values directly from the draw context to the shader parameters.

	The dx11Shader provides 3 different lighting modes in the attribute editor.

	1- "Automatic Bind" (default) Where we try to automatically find the best
	   scene light to drive a set of light parameters on a shader. The scene
	   light is assigned at draw time when refreshing the light parameters of
	   the shader and the assignment is cached in the light parameter info to
	   provide consistent lighting.

	2- Explicit connection: When a user explicitly assign a scene light to a set
	   of shader parameters using the Attribute Editor we explicitely connect the
	   light shape to the "connected_light" attribute of the shader and use this
	   connection to find the corresponding draw context light.

	3- "Use Shader Settings": Where we do not transfer any scene light information
	   and instead use the parameter values currently found in the shader.

	This is tracked using 2 attributes and a cache MObject for each light group.
	The first attribute is a boolean controlling automatic binding, the second
	attribute is used to connect to a light shape for explicit connections, and
	the MObject allows remembering the last used scene light in automatic bind
	mode to prevent lighting to change when a new light is added to the scene:

	                        *_implicit_light
					      | true      | false
		        ----------|-----------|--------------
		*_connected_light |           | Explicit
				connected |  N/A      |   connection
				----------|-----------|--------------
				          | Automatic | Use Shader
			  unconnected |   Bind    |   Settings

	The special case of the ambient light:
	=====================================

	In the draw context, all ambient lights are merged together into a
	single ambient light whose color and intensity is the blend of all
	ambient light present in the scene. When "Automatic Binding" is done
	on such ambient light(s), it will use the merged color value, which is
	subject to change as ambient lights are activated and deactivated.

	Explicit connections to ambient light requires fetching the parameter
	values from the scene light instead of copying the information from
	the merged ambient light from the draw context.

   ====================================================================== */

///////////////////////////////////////////////////////
// This is where we create the light connection attributes
// when a shader is first assigned. When a scene is loaded,
// we only need to retrieve the dynamic attributes that were
// created by the persistence code. The code also handles
// re-creating the attributes if the light group names were
// changed in the effect file.
void dx11ShaderNode::refreshLightConnectionAttributes(bool inSceneUpdateNotification)
{
	if ( inSceneUpdateNotification || (!MFileIO::isReadingFile() && !MFileIO::isOpeningFile()) )
	{
		MFnDependencyNode fnDepThisNode(thisMObject());
		MStatus status;
		for (size_t iLi=0; iLi<fLightParameters.size(); ++iLi)
		{
			LightParameterInfo& currLight(fLightParameters[iLi]);
			MString sanitizedLightGroupName = sanitizeName(fLightNames[(unsigned int)iLi]);

			// If the attributes are not there at this time then create them.
			if (currLight.fAttrUseImplicit.isNull())
				currLight.fAttrUseImplicit = fnDepThisNode.attribute(sanitizedLightGroupName + "_use_implicit_lighting");

			if (currLight.fAttrUseImplicit.isNull())
			{
				// Create:
				MFnNumericAttribute fnAttr;
				MString attrName = sanitizedLightGroupName + "_use_implicit_lighting";
				MObject attrUseImplicit = fnAttr.create(attrName , attrName, MFnNumericData::kBoolean);
				fnAttr.setDefault(true);
				fnAttr.setKeyable(false);
				fnAttr.setStorable(true);
				fnAttr.setAffectsAppearance(true);
				if (!attrUseImplicit.isNull())
				{
					MDGModifier implicitModifier;
					status = implicitModifier.addAttribute(thisMObject(), attrUseImplicit);
					if (status.statusCode() == MStatus::kSuccess)
					{
						status = implicitModifier.doIt();
						if (status.statusCode() == MStatus::kSuccess)
						{
							currLight.fAttrUseImplicit = attrUseImplicit;
						}
					}
				}
			}

			if (currLight.fAttrConnectedLight.isNull())
			{
				currLight.fAttrConnectedLight = fnDepThisNode.attribute(sanitizedLightGroupName + "_connected_light");;
			}
			if (currLight.fAttrConnectedLight.isNull())
			{
				MFnMessageAttribute msgAttr;
				MString attrName = sanitizedLightGroupName + "_connected_light";
				MObject attrConnectedLight = msgAttr.create(attrName, attrName);
				msgAttr.setAffectsAppearance(true);
				if (!attrConnectedLight.isNull())
				{
					MDGModifier implicitModifier;
					status = implicitModifier.addAttribute(thisMObject(), attrConnectedLight);
					if (status.statusCode() == MStatus::kSuccess)
					{
						status = implicitModifier.doIt();
						if (status.statusCode() == MStatus::kSuccess)
						{
							currLight.fAttrConnectedLight = attrConnectedLight;
						}
					}
				}
			}
		}
	}
	else
	{
		// Hmmm. Really not a good idea to start adding parameters while scene is not fullly loaded.
		// Ask to be called back at a later time:
		PostSceneUpdateAttributeRefresher::add(this);
	}
}

/*
	If it is determined that the shader itself may change the geo then we dirty the shadow maps
	Examples of the shader changing geo: time-based effects that change vertex positions, hardware skinning, gpu-cloth, etc.
*/
void dx11ShaderNode::updateShaderBasedGeoChanges()
{
	if (!fShaderChangesGeo)
		return;

	double currentTime = MAnimControl::currentTime().value();
	if ( abs(currentTime - fLastTime) > 0.000000001 )
	{
		fLastTime = currentTime;

		MHWRender::MRenderer::setLightsAndShadowsDirty();
	}
}

/*
	This is where we explicitely connect a light selected by the user
	by creating an explicit connection between the "lightData" of the
	light shape and the "*_connected_light" attribute. This connection
	can be traversed by the Attribute Editor to navigate between the
	dx11Shader and the connected light in both directions.
*/
void dx11ShaderNode::connectLight(int lightIndex, MDagPath lightPath)
{
	if(lightIndex < (int)fLightParameters.size())
	{
		MDGModifier	DG;
		LightParameterInfo& currLight = fLightParameters[lightIndex];

		// Connect the light to the connection placeholder:
		MObject lightShapeNode = lightPath.node();
		MFnDependencyNode dependNode;
		dependNode.setObject(lightShapeNode);
		// Connecting to lightData allows backward navigation:
		MPlug otherPlug = dependNode.findPlug("lightData");
		MPlug paramPlug(thisMObject(),currLight.fAttrConnectedLight);
		MStatus status = DG.connect(otherPlug,paramPlug);
		if(status.statusCode() == MStatus::kSuccess)
		{
			DG.doIt();

			currLight.fIsDirty = true;

			// Lock parameters:
			setLightParameterLocking(currLight, true);

			// Flush implicit cache:
			currLight.fCachedImplicitLight = MObject();

			// Mark the light as being explicitly connected:
			MPlug useImplicitPlug(thisMObject(), currLight.fAttrUseImplicit);
			if( !useImplicitPlug.isNull() ) {
				useImplicitPlug.setValue( false );
			}

			// trigger additional refresh of view to make sure shadow maps are updated
			refreshView();
		}
	}
}

/*
	Helper function to trigger a viewport refresh
	This can be used when we need shadow maps calculated for lights outside the default light list
*/
void dx11ShaderNode::refreshView() const
{
	if (MGlobal::mayaState() != MGlobal::kBatch)
	{
		M3dView view = M3dView::active3dView();
		view.refresh( true /*all views*/, false /*force*/ );
	}
}

/*
	Helper function to set light requires shadow on/off
*/
void dx11ShaderNode::setLightRequiresShadows(const MObject& lightObject, bool requiresShadow) const
{
		if (!lightObject.isNull())
		{
			#if defined(PRINT_DEBUG_INFO_SHADOWS)
						fprintf(stderr, "Clear implicit light path on disconnect light: %s\n", MFnDagNode( lightObject ).fullPathName().asChar());
			#endif

			MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
			theRenderer->setLightRequiresShadows( lightObject, requiresShadow );
		}
}

/*
	Explicitely disconnect an explicit light connection:
*/
void dx11ShaderNode::disconnectLight(int lightIndex)
{
	if(lightIndex < (int)fLightParameters.size())
	{
		LightParameterInfo& currLight = fLightParameters[lightIndex];
		currLight.fIsDirty = true;

		// Unlock all light parameters:
		setLightParameterLocking(currLight, false);

		// Flush implicit cache:
		setLightRequiresShadows(currLight.fCachedImplicitLight, false);
		currLight.fCachedImplicitLight = MObject();

		// Disconnect the light from the connection placeholder:
		{
			MFnDependencyNode thisDependNode;
			thisDependNode.setObject(thisMObject());
			MPlug thisLightConnectionPlug = thisDependNode.findPlug(currLight.fAttrConnectedLight, true);
			if (thisLightConnectionPlug.isConnected())
			{
				// Find the light connected as source to this plug:
				MPlugArray srcCnxArray;
				thisLightConnectionPlug.connectedTo(srcCnxArray,true,false);
				if (srcCnxArray.length() > 0)
				{
					MPlug sourcePlug = srcCnxArray[0];
					MDGModifier	DG;
					DG.disconnect(sourcePlug, thisLightConnectionPlug);
					DG.doIt();

					setLightRequiresShadows(sourcePlug.node(), false);

					// trigger additional refresh of view to make sure shadow maps are updated
					refreshView();
				}
			}
		}
	}
}

/*
	Implicit light connection:
	=========================

	In this function we want to bind the M shader lights to the best
	subset of the N scene lights found in the draw context. For performance
	we keep count of the number of light to connect and short-circuit loops
	when we ran out of lights to bind on either the shader or draw context side.

	This function can be called in 3 different context:

	- Scene: We have multiple lights in the draw context and we need to
	         find a light that is compatible with the shader whenever the
			 cached light is not found and it is not explicitly connected.
	- Default light: The draw context will contain only a single light and
	                 it needs to override light in all three lighting modes.
	- Swatch: Same requirements as "Default Light", but does not override
	          lights in "Use Shader Settings" mode.

	We need to keep track of which lights are implicitly/explicitly bound to
	make sure we do not automatically bind the same light more than once.

	Scene ligths that are part of the scene but cannot be found in the draw
	context are either invisible, disabled, or in any other lighting combination
	(like "Use Selected Light") where we do not want to see the lighting in the
	shader. For these lights we turn the shader lighting "off" by setting
	the shader parameter values to black, with zero intensity.
*/
void dx11ShaderNode::updateImplicitLightConnections(const MHWRender::MDrawContext& context, ERenderType renderType) const
{
	if(renderType != RENDER_SCENE && renderType != RENDER_SWATCH)
		return;

	bool ignoreLightLimit = true;
	MHWRender::MDrawContext::LightFilter lightFilter = MHWRender::MDrawContext::kFilteredToLightLimit;
	if (ignoreLightLimit)
	{
		lightFilter = MHWRender::MDrawContext::kFilteredIgnoreLightLimit;
	}
	unsigned int nbSceneLights = context.numberOfActiveLights(lightFilter);
	unsigned int nbSceneLightsToBind = nbSceneLights;
	bool implicitLightWasRebound = false;

	// Detect headlamp scene rendering mode:
	if(renderType == RENDER_SCENE && nbSceneLights == 1)
	{
		MHWRender::MLightParameterInformation* sceneLightParam = context.getLightParameterInformation( 0 );
		const ELightType sceneLightType = getLightType(sceneLightParam);
		if(sceneLightType == eDefaultLight )
		{
			// Swatch and headlamp are the same as far as
			// implicit light connection is concerned:
			renderType = RENDER_SCENE_DEFAULT_LIGHT;
		}
	}

	unsigned int nbShaderLights = (unsigned int)fLightParameters.size();
	unsigned int nbShaderLightsToBind = nbShaderLights;
	// Keep track of the shader lights that were treated : binding was successful
	std::vector<bool> shaderLightTreated(nbShaderLights, false);
	std::vector<bool> shaderLightUsesImplicit(nbShaderLights, false);

	MFnDependencyNode depFn( thisMObject() );

	// Keep track of the scene lights that were used : binding was successful
	std::vector<bool> sceneLightUsed(nbSceneLights, false);

	// Upkeep pass.
	//
	// We want to know exactly which shader light will later require implicit
	// connection, and which scene lights are already used. We also remember
	// lights that were previously bound using the cached light parameter of
	// the light group info structure. It the cached light exists, and is
	// still available for automatic binding, we immediately reuse it.
	if(renderType == RENDER_SCENE)
	{
		// Find out all explicitely connected lights and mark them as already
		// bound.
		for(unsigned int shaderLightIndex = 0;
			shaderLightIndex < nbShaderLights && nbShaderLightsToBind && nbSceneLightsToBind;
			++shaderLightIndex )
		{
			const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];
			MPlug thisLightConnectionPlug = depFn.findPlug(shaderLightInfo.fAttrConnectedLight, true);
			if (thisLightConnectionPlug.isConnected())
			{
				// Find the light connected as source to this plug:
				MPlugArray srcCnxArray;
				thisLightConnectionPlug.connectedTo(srcCnxArray,true,false);
				if (srcCnxArray.length() > 0)
				{
					MPlug sourcePlug = srcCnxArray[0];
					for(unsigned int sceneLightIndex = 0; sceneLightIndex < nbSceneLights; ++sceneLightIndex)
					{
						MHWRender::MLightParameterInformation* sceneLightParam = context.getLightParameterInformation( sceneLightIndex, lightFilter );
						if(sceneLightParam->lightPath().node() == sourcePlug.node())
						{
							sceneLightUsed[sceneLightIndex] = true;
							nbSceneLightsToBind--;
						}
					}
					if (!shaderLightInfo.fCachedImplicitLight.isNull())
					{
						(const_cast<LightParameterInfo&>(shaderLightInfo)).fCachedImplicitLight = MObject();
						// Light is explicitely connected, so parameters are locked:
						setLightParameterLocking(shaderLightInfo, true);
						implicitLightWasRebound = true;
					}
				}
			}
		}

		// Update cached implicit lights:
		for(unsigned int shaderLightIndex = 0;
			shaderLightIndex < nbShaderLights && nbShaderLightsToBind;
			++shaderLightIndex )
		{
			// See if this light uses implicit connections:
			const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];
			MPlug useImplicitPlug = depFn.findPlug( shaderLightInfo.fAttrUseImplicit, false );
			if( !useImplicitPlug.isNull() ) {
				bool useImplicit;
				useImplicitPlug.getValue( useImplicit );
				shaderLightUsesImplicit[shaderLightIndex] = useImplicit;
				if (useImplicit)
				{
					// Make sure cached light is still in model:
					if (!shaderLightInfo.fCachedImplicitLight.isNull())
					{
						MStatus status;
						MFnDagNode lightDagNode(shaderLightInfo.fCachedImplicitLight, &status);
						if (status.statusCode() == MStatus::kSuccess && lightDagNode.inModel() ) {

							// Try to connect to the cached light:
							MHWRender::MLightParameterInformation* matchingSceneLightParam = NULL;
							unsigned int sceneLightIndex = 0;

							for( ; sceneLightIndex < nbSceneLights; ++sceneLightIndex)
							{
								MHWRender::MLightParameterInformation* sceneLightParam = context.getLightParameterInformation( sceneLightIndex, lightFilter );

								if( sceneLightParam->lightPath().node() == shaderLightInfo.fCachedImplicitLight )
								{
									matchingSceneLightParam = sceneLightParam;
									break;
								}
							}

							if (matchingSceneLightParam)
							{
								if (!sceneLightUsed[sceneLightIndex])
								{
									connectLight(shaderLightInfo, matchingSceneLightParam);
									sceneLightUsed[sceneLightIndex] = true;			// mark this scene light as used
									nbSceneLightsToBind--;
									shaderLightTreated[shaderLightIndex] = true;	// mark this shader light as binded
									nbShaderLightsToBind--;
								}
								else
								{
									setLightRequiresShadows(shaderLightInfo.fCachedImplicitLight, false);

									// Light already in use, clear the cache to allow binding at a later stage:
									(const_cast<LightParameterInfo&>(shaderLightInfo)).fCachedImplicitLight = MObject();
									setLightParameterLocking(shaderLightInfo, false);
									implicitLightWasRebound = true;
								}
							}
							else
							{
								// mark this shader light as bound even if not found in DC
								turnOffLight(shaderLightInfo);
								shaderLightTreated[shaderLightIndex] = true;
								nbShaderLightsToBind--;
							}
						}
						else
						{
							// Note that we don't need to clear the requirement for
							// implicit shadow maps here as light deletion is already handled by the renderer
							//
							// Light is not in the model anymore, allow rebinding:
							(const_cast<LightParameterInfo&>(shaderLightInfo)).fCachedImplicitLight = MObject();
							setLightParameterLocking(shaderLightInfo, false);
							implicitLightWasRebound = true;
						}
					}
				}
				else
				{
					// This light is either explicitly bound, or in the
					// "Use Shader Settings" mode, so we have one less
					// shader light to bind:
					nbShaderLightsToBind--;
				}
			}
		}
	}
	else
	{
		// Here we are in swatch or default light mode and must override all light connection
		// by marking them all as available for "Automatic Bind"
		for(unsigned int shaderLightIndex = 0;
			shaderLightIndex < nbShaderLights && nbShaderLightsToBind && nbSceneLightsToBind;
			++shaderLightIndex )
		{
			const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];
			MPlug thisLightConnectionPlug = depFn.findPlug(shaderLightInfo.fAttrConnectedLight, true);

			bool useImplicit = true;
			MPlug useImplicitPlug = depFn.findPlug( shaderLightInfo.fAttrUseImplicit, false );
			if( !useImplicitPlug.isNull() ) {
				useImplicitPlug.getValue( useImplicit );
			}

			if (thisLightConnectionPlug.isConnected() || useImplicit || renderType == RENDER_SCENE_DEFAULT_LIGHT )
			{
				shaderLightUsesImplicit[shaderLightIndex] = true;
			}
			else
			{
				// In swatch rendering, lights in the "Use Shader Settings" mode are not
				// overridden:
				nbShaderLightsToBind--;
			}
		}
	}

	// First pass ... try to connect each shader lights with the best scene light possible.
	// This means for each light whose type is explicitly known, we try to find the first
	// draw context light that is of the same type.
	//
	// The type of the shader light is deduced automatically first by looking for a substring
	// match in the light "Object" annotation, then by searching the parameter name, and finally
	// by checking which combination of position/direction semantics the light requires:
	if(renderType == RENDER_SCENE)
		fImplicitAmbientLight = -1;

	for(unsigned int shaderLightIndex = 0;
		shaderLightIndex < nbShaderLights && nbShaderLightsToBind && nbSceneLightsToBind;
		++shaderLightIndex )
	{
		const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];
		const ELightType shaderLightType = shaderLightInfo.lightType();

		if(!shaderLightUsesImplicit[shaderLightIndex] || shaderLightTreated[shaderLightIndex] == true)
			continue;

		for(unsigned int sceneLightIndex = 0; sceneLightIndex < nbSceneLights; ++sceneLightIndex)
		{
			if(sceneLightUsed[sceneLightIndex] == true)
				continue;

			MHWRender::MLightParameterInformation* sceneLightParam = context.getLightParameterInformation( sceneLightIndex, lightFilter );

			const ELightType sceneLightType = getLightType(sceneLightParam);
			if( shaderLightType == sceneLightType || shaderLightInfo.fHasLightTypeSemantics )
			{
				connectLight(shaderLightInfo, sceneLightParam, renderType);

				shaderLightTreated[shaderLightIndex] = true;	// mark this shader light as binded
				nbShaderLightsToBind--;

				// Rendering swatch needs to drive all lights, except if they have a light type semantics,
				// where we only need to drive one:
				if (renderType != RENDER_SWATCH || shaderLightInfo.fHasLightTypeSemantics)
				{
					sceneLightUsed[sceneLightIndex] = true;			// mark this scene light as used
					nbSceneLightsToBind--;
				}

				if(renderType == RENDER_SCENE)
				{
					setLightRequiresShadows(shaderLightInfo.fCachedImplicitLight, true);

					(const_cast<LightParameterInfo&>(shaderLightInfo)).fCachedImplicitLight = sceneLightParam->lightPath().node();
					setLightParameterLocking(shaderLightInfo, true);
					implicitLightWasRebound = true;

					// only update 'fImplicitAmbientLight' if it was not set yet. This allows the user to
					// manually bind an ambient light into the shader and still see any implicit 'Ambient' lighting bound in AE.
					if (sceneLightType == eAmbientLight && fImplicitAmbientLight < 0)
						fImplicitAmbientLight = shaderLightIndex;
				}
				else
				{
					// Will need to refresh defaults on next scene redraw:
					(const_cast<LightParameterInfo&>(shaderLightInfo)).fIsDirty = true;
				}

				break;
			}
		}
	}

	// Second pass ... connect remaining shader lights with scene lights that are not yet connected.
	//
	// In this pass, we consider compatible all lights that possess a superset of the
	// semantics required by the shader light, so a scene spot light can be bound to
	// shader lights requesting only a position, or a direction, and any light can bind
	// to a shader light that only requires a color:
	for(unsigned int shaderLightIndex = 0;
		shaderLightIndex < nbShaderLights && nbShaderLightsToBind && nbSceneLightsToBind;
		++shaderLightIndex )
	{
		if(!shaderLightUsesImplicit[shaderLightIndex] || shaderLightTreated[shaderLightIndex] == true)
			continue;

		const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];
		const ELightType shaderLightType = shaderLightInfo.lightType();

		for(unsigned int sceneLightIndex = 0; sceneLightIndex < nbSceneLights; ++sceneLightIndex)
		{
			if(sceneLightUsed[sceneLightIndex] == true)
				continue;

			MHWRender::MLightParameterInformation* sceneLightParam = context.getLightParameterInformation( sceneLightIndex, lightFilter );

			const ELightType sceneLightType = getLightType(sceneLightParam);
			if( isLightAcceptable(shaderLightType, sceneLightType) )
			{
				connectLight(shaderLightInfo, sceneLightParam, renderType);

				shaderLightTreated[shaderLightIndex] = true;	// mark this shader light as binded
				nbShaderLightsToBind--;

				// Rendering swatch needs to drive all lights, except if they have a light type semantics,
				// where we only need to drive one:
				if (renderType != RENDER_SWATCH || shaderLightInfo.fHasLightTypeSemantics)
				{
					sceneLightUsed[sceneLightIndex] = true;			// mark this scene light as used
					nbSceneLightsToBind--;
				}

				if(renderType == RENDER_SCENE)
				{
					(const_cast<LightParameterInfo&>(shaderLightInfo)).fCachedImplicitLight = sceneLightParam->lightPath().node();
					setLightParameterLocking(shaderLightInfo, true);
					implicitLightWasRebound = true;

					setLightRequiresShadows(shaderLightInfo.fCachedImplicitLight, true);
				}
				else
				{
					// Will need to refresh defaults on next scene redraw:
					(const_cast<LightParameterInfo&>(shaderLightInfo)).fIsDirty = true;
				}

				break;
			}
		}
	}

	// Final pass: shutdown all implicit lights that were not bound
	for(unsigned int shaderLightIndex = 0;
		shaderLightIndex < nbShaderLights && nbShaderLightsToBind;
		++shaderLightIndex )
	{
		if(!shaderLightUsesImplicit[shaderLightIndex] || shaderLightTreated[shaderLightIndex] == true)
			continue;

		const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];

		turnOffLight(shaderLightInfo);

		if(renderType != RENDER_SCENE)
		{
			// Will need to refresh defaults on next scene redraw:
			(const_cast<LightParameterInfo&>(shaderLightInfo)).fIsDirty = true;
		}
	}

	// If during this update phase we changed any of the cached implicit light
	// objects, we need to trigger a refresh of the attribute editor light binding
	// information to show the current light connection settings. Multiple requests
	// are pooled by the refresher and only one request is sent to the AE in the next
	// idle window.
	if (implicitLightWasRebound)
		IdleAttributeEditorImplicitRefresher::activate();
}

/*
	Traverse all explicit light connections and refresh the shader data if the light
	is found in the draw context, otherwise turn off the light.

	This is also where we handle the special case of the merged ambient lights by
	refreshing the connected ambient light, but only if we found the merged one
	inside the draw context. Not finding ambient lights in the draw context mean that
	they are all invisible, or disabled, or otherwise not drawn.

*/
void dx11ShaderNode::updateExplicitLightConnections(const MHWRender::MDrawContext& context, ERenderType renderType) const
{
	if(renderType != RENDER_SCENE)
		return;

	unsigned int nbShaderLights = (unsigned int)fLightParameters.size();
	if(nbShaderLights < 0)
		return;

	bool ignoreLightLimit = true;
	MHWRender::MDrawContext::LightFilter lightFilter = MHWRender::MDrawContext::kFilteredToLightLimit;
	if (ignoreLightLimit)
	{
		lightFilter = MHWRender::MDrawContext::kFilteredIgnoreLightLimit;
	}
	unsigned int nbSceneLights = context.numberOfActiveLights(lightFilter);

	MFnDependencyNode thisDependNode;
	thisDependNode.setObject(thisMObject());

	for(size_t shaderLightIndex = 0; shaderLightIndex <nbShaderLights; ++shaderLightIndex )
	{
		const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];

		MPlug thisLightConnectionPlug = thisDependNode.findPlug(shaderLightInfo.fAttrConnectedLight, true);
		if (thisLightConnectionPlug.isConnected())
		{
			// Find the light connected as source to this plug:
			MPlugArray srcCnxArray;
			thisLightConnectionPlug.connectedTo(srcCnxArray,true,false);
			if (srcCnxArray.length() > 0)
			{
				MPlug sourcePlug = srcCnxArray[0];
				MObject sourceLight(sourcePlug.node());
				bool bHasAmbient = false;

				bool bLightEnabled = false;
				unsigned int sceneLightIndex = 0;
				for(; sceneLightIndex < nbSceneLights; ++sceneLightIndex)
				{
					MHWRender::MLightParameterInformation* sceneLightParam = context.getLightParameterInformation( sceneLightIndex, lightFilter );
					if(sceneLightParam->lightPath().node() == sourceLight)
					{
						setLightRequiresShadows(sourceLight, true);

						// Use connectLight to transfer all values.
						connectLight(shaderLightInfo, sceneLightParam);

						// Keep light visibility state in case shader cares:
						MFloatArray floatVals;
						static MString kLightOn("lightOn");
						sceneLightParam->getParameter( kLightOn, floatVals );
						bLightEnabled = (floatVals.length() == 0 || floatVals[0] > 0) ? true : false;
						break;
					}

					if (eAmbientLight == getLightType(sceneLightParam))
					{
						bHasAmbient = true;
						bLightEnabled = true;
					}
				}

				if (bHasAmbient && sceneLightIndex == nbSceneLights)
					bLightEnabled = connectExplicitAmbientLight(shaderLightInfo, sourceLight);

				// Adjust LightEnable parameter if it exists based on the presence of the light in the draw context:
				if (!bLightEnabled)
				{
					turnOffLight(shaderLightInfo);
				}
			}
		}
	}
}

/*
	This function rebuilds all the shader light information structures:

	fLightParameters: Main struct that contains the frequently use runtime information
		Contains:
			fLightType: What kind of scene light drives this shader light completely
			fHasLightTypeSemantics: Is the shader light code able to adapt to multiple light types?
			fIsDirty: Should we refresh the shader light parameter values at the next redraw?
			fConnectableParameters: Set of indices in the uniform parameter array that define this shader light
			fAttrUseImplicit: Boolean attribute whose value is true when in "Automatic Bind" mode
			fAttrConnectedLight: Message attribute that is connected to a light shape for explicit binds
			fCachedImplicitLight: Reference to the light shape that was automatically bound during last redraw

	 fLightDescriptions: String array containing pairs of (Light Group Name, Light Group Type) returned by
	                     "dx11Shader -listLightInformation" query and used by the AE to create the light
						 connection panel and to filter which scene lights can appear in the dropdowns for
						 explicit connection
*/
void dx11ShaderNode::updateImplicitLightParameterCache(std::vector<CUniformParameterBuilder*>& builders)
{
	MFnDependencyNode fnDepThisNode(thisMObject());
	MDGModifier implicitModifier;

	// The attributes for connected lights and implicit binding can be created from
	// the persistence. Try to preserve them if possible.
	bool updateConnectionAttributes = ( !MFileIO::isReadingFile() && !MFileIO::isOpeningFile() );
	if ( updateConnectionAttributes ) {
		// Do not update if the light groups are exactly the same:
		//   (happens a lot when switching from one technique to another)
		if ( fLightParameters.size() == fLightNames.length() )
		{
			updateConnectionAttributes = false;
			for (size_t iLi=0; iLi<fLightParameters.size(); ++iLi) {
				MString newName = sanitizeName(fLightNames[(unsigned int)iLi]) + "_use_implicit_lighting";
				MStatus status;
				MFnAttribute currentAttribute(fLightParameters[iLi].fAttrUseImplicit, &status);
				if (status.statusCode() != MStatus::kSuccess || currentAttribute.name() != newName ) {
					updateConnectionAttributes = true;
					break;
				}
			}
		}
	}

	if ( updateConnectionAttributes ) {
		for (size_t iLi=0; iLi<fLightParameters.size(); ++iLi)
		{
			if(fLightParameters[iLi].fAttrUseImplicit.isNull() == false)
				implicitModifier.removeAttribute(thisMObject(), fLightParameters[iLi].fAttrUseImplicit);
			if(fLightParameters[iLi].fAttrConnectedLight.isNull() == false)
				implicitModifier.removeAttribute(thisMObject(), fLightParameters[iLi].fAttrConnectedLight);
		}
	}
	implicitModifier.doIt();

	fLightParameters.clear();
	fLightParameters.resize(fLightNames.length());
	refreshLightConnectionAttributes();

	/*
		first loop over all uniform parameters to find out which parameters belong to
		which light group, to find out if a shader light group defines an "intelligent"
		light that has code that can adapt to any connected light type, and to find out
		if the CUniformParameterBuilder was able to deduce the light type using either
		the light group name, or by finding a semantic type that is exclusive to one
		light type (like cone angle).
	*/
	std::vector<CUniformParameterBuilder*>::iterator iter = builders.begin();
	int index = 0;
	CUniformParameterBuilder::ELightType currLightType = CUniformParameterBuilder::eNotLight;
	CUniformParameterBuilder::ELightParameterType paramType;
	for(;iter != builders.end();++iter,++index)
	{
		CUniformParameterBuilder* currBuilder = *iter;

		if(!currBuilder->isValidUniformParameter())
			continue;

		int lightIndex = currBuilder->getLightIndex();
		if (lightIndex < 0)
			continue;

		LightParameterInfo& currLight(fLightParameters[lightIndex]);

		// A shader parameter can have a light group "Object" annotation but have
		// no recognized light semantics. In this case we do not need to add
		// this parameter to the light parameter set.
		if(currBuilder->getLightType() !=  CUniformParameterBuilder::eNotLight)
		{
			if(currLightType == CUniformParameterBuilder::eNotLight)
			{
				currLightType = currBuilder->getLightType();
			}

			paramType = currBuilder->getLightParameterType();
			if (paramType == CUniformParameterBuilder::eLightType)
			{
				// This light can be connected to any scene light and react correctly:
				currLight.fHasLightTypeSemantics = true;
			}

			currLight.fConnectableParameters.insert(LightParameterInfo::TConnectableParameters::value_type(index, paramType));

			switch(currBuilder->getLightType())
			{
			case CUniformParameterBuilder::eUndefinedLight:
				currLight.fLightType = eUndefinedLight;
				break;
			case CUniformParameterBuilder::eSpotLight:
				currLight.fLightType = eSpotLight;
				break;
			case CUniformParameterBuilder::ePointLight:
				currLight.fLightType = ePointLight;
				break;
			case CUniformParameterBuilder::eDirectionalLight:
				currLight.fLightType = eDirectionalLight;
				break;
			case CUniformParameterBuilder::eAmbientLight:
				currLight.fLightType = eAmbientLight;
				break;
			case CUniformParameterBuilder::eAreaLight:
				currLight.fLightType = eAreaLight;
				break;
			default:
				break;
			};
		}
	}

	/*
		Once all light group information is found, we can generate
		the light parameter info array for the AE
	*/
	fLightDescriptions.clear();
	LightParameterInfoVec::iterator iterLight = fLightParameters.begin();
	unsigned int lightIndex = 0;
	for(;iterLight != fLightParameters.end();++iterLight, ++lightIndex)
	{
		fLightDescriptions.append(fLightNames[lightIndex]);

		static const MString kInvalid("invalid");
		static const MString kUndefined("undefined");
		static const MString kSpot("spot");
		static const MString kPoint("point");
		static const MString kDirectional("directional");
		static const MString kAmbient("ambient");
		static const MString kArea("area");

		MString lightType = kInvalid;
		switch(iterLight->fLightType)
		{
		case eUndefinedLight:
			lightType = kUndefined;
			break;
		case eSpotLight:
			lightType = kSpot;
			break;
		case ePointLight:
			lightType = kPoint;
			break;
		case eDirectionalLight:
			lightType = kDirectional;
			break;
		case eAmbientLight:
			lightType = kAmbient;
			break;
		case eAreaLight:
			lightType = kArea;
			break;
		};
		fLightDescriptions.append(lightType);
	}
}

void dx11ShaderNode::clearLightConnectionData()
{
	// Unlock all light parameters.
	for (size_t i = 0; i < fLightParameters.size(); ++i) {
		fLightParameters[i].fCachedImplicitLight = MObject();
		setLightParameterLocking(fLightParameters[i], false);
	}

	fLightNames.setLength(0);
	fUIGroupNames.setLength(0);
	fUIGroupParameters.clear();
	fLightDescriptions.setLength(0);
}

/*
	Populates the set of light parameters that need to be refreshed from the shader parameter
	values in this redraw. This includes all parameters in any light group that was marked as
	being dirty, and can also include parameters from clean groups if the rendering context
	is swatch or default light since the light binding can be overridden.

	Light groups will get dirty in the following scenarios:
		- A notification from a connected light shape was received
		- A scene light was explicitely connected or disconnected
		- Last draw was done in swatch or default scene light context
*/
void dx11ShaderNode::getLightParametersToUpdate(std::set<int>& parametersToUpdate, ERenderType renderType) const
{
	for(size_t shaderLightIndex = 0; shaderLightIndex < fLightParameters.size(); ++shaderLightIndex )
	{
		const LightParameterInfo& shaderLightInfo = fLightParameters[shaderLightIndex];

		if (shaderLightInfo.fIsDirty || renderType != RENDER_SCENE)
		{
			LightParameterInfo::TConnectableParameters::const_iterator it = shaderLightInfo.fConnectableParameters.begin();
			LightParameterInfo::TConnectableParameters::const_iterator itEnd = shaderLightInfo.fConnectableParameters.end();
			for (; it != itEnd; ++it)
			{
				parametersToUpdate.insert(it->first);
			}

			if (renderType == RENDER_SCENE)
			{
				// If light is implicit, it stays dirty (as we do not control
				// what happens with the lights and need to react quickly)
				MFnDependencyNode depFn( thisMObject() );
				MPlug useImplicitPlug = depFn.findPlug( shaderLightInfo.fAttrUseImplicit, false );
				if( !useImplicitPlug.isNull() ) {
					bool useImplicit;
					useImplicitPlug.getValue( useImplicit );
					if (!useImplicit)
					{
						// Light will be cleaned. And we are not implicit.
						(const_cast<LightParameterInfo&>(shaderLightInfo)).fIsDirty = false;
					}
				}
			}
		}
	}
}

/*
	Transfer light parameter values from a draw context light info to all shader parameters
	of the specified light group. Uses the drawContextParameterNames acceleration structure
	to iterate quickly through relevant draw context parameters.
*/
void dx11ShaderNode::connectLight(const LightParameterInfo& lightInfo, MHWRender::MLightParameterInformation* lightParam, ERenderType renderType) const
{
	unsigned int positionCount = 0;
	MFloatPoint position;
	MFloatVector direction;
	float intensity = 1.0f;
	float decayRate = 0.0f;
	MColor color(1.0f, 1.0f, 1.0f);
	bool globalShadowsOn = false;
	bool localShadowsOn = false;
	ID3D11ShaderResourceView *shadowResource = NULL;
	MMatrix shadowViewProj;
	MColor shadowColor;
	float shadowBias = 0.0f;
	MAngle hotspot(40.0, MAngle::kDegrees);
	MAngle falloff(0.0);

	ELightType lightType = getLightType(lightParam);

	// Looping on the uniform parameters reduces the processing time by not
	// enumerating light parameters that are not used by the shader.
	LightParameterInfo::TConnectableParameters::const_iterator it    = lightInfo.fConnectableParameters.begin();
	LightParameterInfo::TConnectableParameters::const_iterator itEnd = lightInfo.fConnectableParameters.end();
	for (; it != itEnd; ++it)
	{
		const int parameterIndex = it->first;
		const int parameterType  = it->second;

		if (parameterType == CUniformParameterBuilder::eLightType) {
			setParameterAsScalar(parameterIndex, lightType != dx11ShaderNode::eDefaultLight? (int)lightType : dx11ShaderNode::eDirectionalLight);
			continue;
		}

		if (parameterType == CUniformParameterBuilder::eLightEnable) {
			setParameterAsScalar(parameterIndex, true);
			continue;
		}

		const MStringArray& params(drawContextParameterNames(lightType, parameterType, lightParam));

		if (params.length() == 0)
			continue;

		for (unsigned int p = 0; p < params.length(); ++p)
		{
			MString pname = params[p];

			MHWRender::MLightParameterInformation::StockParameterSemantic semantic = lightParam->parameterSemantic( pname );

			// Pull off values with position, direction, intensity or color
			// semantics
			//
			MFloatArray floatVals;
			MIntArray intVals;

			switch (semantic)
			{
			case MHWRender::MLightParameterInformation::kWorldPosition:
				lightParam->getParameter( pname, floatVals );
				position += MFloatPoint( floatVals[0], floatVals[1], floatVals[2] );
				++positionCount;
				break;
			case MHWRender::MLightParameterInformation::kWorldDirection:
				lightParam->getParameter( pname, floatVals );
				direction = MFloatVector( floatVals[0], floatVals[1], floatVals[2] );
				break;
			case MHWRender::MLightParameterInformation::kIntensity:
				lightParam->getParameter( pname, floatVals );
				intensity = floatVals[0];
				break;
			case MHWRender::MLightParameterInformation::kDecayRate:
				lightParam->getParameter( pname, floatVals );
				decayRate = floatVals[0];
				break;
			case MHWRender::MLightParameterInformation::kColor:
				lightParam->getParameter( pname, floatVals );
				color[0] = floatVals[0];
				color[1] = floatVals[1];
				color[2] = floatVals[2];
				break;
			// Parameter type extraction for shadow maps
			case MHWRender::MLightParameterInformation::kGlobalShadowOn:
				lightParam->getParameter( pname, intVals );
				if (intVals.length())
					globalShadowsOn = (intVals[0] != 0) ? true : false;
				break;
			case MHWRender::MLightParameterInformation::kShadowOn:
				lightParam->getParameter( pname, intVals );
				if (intVals.length())
					localShadowsOn = (intVals[0] != 0) ? true : false;
				break;
			case MHWRender::MLightParameterInformation::kShadowViewProj:
				lightParam->getParameter( pname, shadowViewProj);
				break;
			case MHWRender::MLightParameterInformation::kShadowMap:
				shadowResource = (ID3D11ShaderResourceView *) lightParam->getParameterTextureHandle( pname );
				break;
			case MHWRender::MLightParameterInformation::kShadowColor:
				lightParam->getParameter( pname, floatVals );
				shadowColor[0] = floatVals[0];
				shadowColor[1] = floatVals[1];
				shadowColor[2] = floatVals[2];
				break;
			case MHWRender::MLightParameterInformation::kShadowBias:
				lightParam->getParameter(pname,floatVals);
				shadowBias = floatVals[0];
				break;
			case MHWRender::MLightParameterInformation::kCosConeAngle:
				lightParam->getParameter(pname,floatVals);
				hotspot = MAngle(acos(floatVals[0]), MAngle::kRadians);
				falloff = MAngle(acos(floatVals[1]), MAngle::kRadians);
				break;
			default:
				break;
			}
		}

		// Compute an average position in case we connected an area
		// light to a shader light that cannot handle the 4 corners:
		if (positionCount > 1)
		{
			position[0] /= (float)positionCount;
			position[1] /= (float)positionCount;
			position[2] /= (float)positionCount;
		}

		switch (parameterType)
		{
		case CUniformParameterBuilder::eLightColor:
		case CUniformParameterBuilder::eLightAmbientColor:
		case CUniformParameterBuilder::eLightSpecularColor:
		case CUniformParameterBuilder::eLightDiffuseColor:
			{
				// For swatch and headlamp, we need to tone down the color if it is driving an ambient light:
				if (renderType != RENDER_SCENE && lightInfo.fLightType == eAmbientLight)
				{
					color[0] *= 0.15f;
					color[1] *= 0.15f;
					color[2] *= 0.15f;
				}

				//update color
				setParameterAsVector(parameterIndex, (float*)&color[0]);
			}
			break;

		case CUniformParameterBuilder::eLightPosition:
		case CUniformParameterBuilder::eLightAreaPosition0:
		case CUniformParameterBuilder::eLightAreaPosition1:
		case CUniformParameterBuilder::eLightAreaPosition2:
		case CUniformParameterBuilder::eLightAreaPosition3:
			setParameterAsVector(parameterIndex, (float*)&position[0]);
			positionCount = 0;
			position = MFloatPoint();
			break;

		case CUniformParameterBuilder::eLightIntensity:
			setParameterAsScalar(parameterIndex, intensity);
			break;

		case CUniformParameterBuilder::eDecayRate:
			setParameterAsScalar(parameterIndex, decayRate);
			break;

		case CUniformParameterBuilder::eLightDirection:
			setParameterAsVector(parameterIndex, (float*)&direction[0]);
			break;

		case CUniformParameterBuilder::eLightShadowMapBias:
			setParameterAsScalar(parameterIndex, shadowBias);
			break;

		case CUniformParameterBuilder::eLightShadowColor:
			setParameterAsVector(parameterIndex, (float*)&shadowColor[0]);
			break;

		case CUniformParameterBuilder::eLightShadowOn:
		{
			// Do an extra check to make sure we have an up-to-date shadow map.
			// If not, disable shadows.
			bool localShadowsDirty = false;
			MIntArray intVals;
			lightParam->getParameter(MHWRender::MLightParameterInformation::kShadowDirty, intVals );
			if (intVals.length())
				localShadowsDirty = (intVals[0] != 0) ? true : false;

			setParameterAsScalar(parameterIndex, globalShadowsOn && localShadowsOn && shadowResource &&
				!localShadowsDirty);
			break;
		}
		case CUniformParameterBuilder::eLightShadowViewProj:
			setParameterAsMatrix(parameterIndex, shadowViewProj);
			break;

		case CUniformParameterBuilder::eLightShadowMap:
			setParameterAsResource(parameterIndex, shadowResource);
			break;

		case CUniformParameterBuilder::eLightHotspot:
			setParameterAsScalar(parameterIndex, float(hotspot.asRadians()));
			break;

		case CUniformParameterBuilder::eLightFalloff:
			setParameterAsScalar(parameterIndex, float(falloff.asRadians()));
			break;

		default:
			break;
		}
	}
}

bool dx11ShaderNode::connectExplicitAmbientLight(const LightParameterInfo& lightInfo, const MObject& sourceLight) const
{
	bool bDidConnect = false;
	if (sourceLight.hasFn(MFn::kAmbientLight))
	{
		MStatus status;
		MFnAmbientLight ambientLight(sourceLight, &status);

		if (status == MStatus::kSuccess)
		{
			bDidConnect = true;
			LightParameterInfo::TConnectableParameters::const_iterator it    = lightInfo.fConnectableParameters.begin();
			LightParameterInfo::TConnectableParameters::const_iterator itEnd = lightInfo.fConnectableParameters.end();
			for (; it != itEnd; ++it)
			{
				const int parameterIndex = it->first;
				const int parameterType  = it->second;

				switch (parameterType)
				{
				case CUniformParameterBuilder::eLightType:
					setParameterAsScalar(parameterIndex, (int)eAmbientLight);
					break;

				case CUniformParameterBuilder::eLightEnable:
					setParameterAsScalar(parameterIndex, true);
					break;

				case CUniformParameterBuilder::eLightColor:
				case CUniformParameterBuilder::eLightAmbientColor:
				case CUniformParameterBuilder::eLightSpecularColor:
				case CUniformParameterBuilder::eLightDiffuseColor:
					{
						//update color
						MColor ambientColor(ambientLight.color());
						float color[3];
						ambientColor.get(color);
						setParameterAsVector(parameterIndex, color);
					}
					break;

				case CUniformParameterBuilder::eLightIntensity:
					setParameterAsScalar(parameterIndex, ambientLight.intensity());
					break;
				}
			}
		}
	}
	return bDidConnect;
}

void dx11ShaderNode::turnOffLight(const LightParameterInfo& lightInfo) const
{
	static const float kOffColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

	LightParameterInfo::TConnectableParameters::const_iterator it;
	for (it = lightInfo.fConnectableParameters.begin();
			it != lightInfo.fConnectableParameters.end(); ++it)
	{
		const int parameterIndex = it->first;
		const int parameterType  = it->second;
		switch (parameterType)
		{
		case CUniformParameterBuilder::eLightEnable:
			setParameterAsScalar(parameterIndex, false);
			break;

		case CUniformParameterBuilder::eLightColor:
		case CUniformParameterBuilder::eLightAmbientColor:
		case CUniformParameterBuilder::eLightSpecularColor:
		case CUniformParameterBuilder::eLightDiffuseColor:
			setParameterAsVector(parameterIndex, (float*)kOffColor);
			break;

		case CUniformParameterBuilder::eLightIntensity:
			setParameterAsScalar(parameterIndex, 0.0f);
			break;

		}
	}
}

/*
	When a shader light is driver either by an explicit light connection or has been bound
	once to a scene light while in "Automatic Bind" mode, we need to make all attributes
	uneditable in the attribute editor.

	This function locks and unlocks light parameters as connection come and go:
*/
void dx11ShaderNode::setLightParameterLocking(const LightParameterInfo& lightInfo, bool locked) const
{
	for (LightParameterInfo::TConnectableParameters::const_iterator idxIter=lightInfo.fConnectableParameters.begin();
		idxIter != lightInfo.fConnectableParameters.end();
		++idxIter)
	{
		int parameterIndex((*idxIter).first);
		MUniformParameter param = fUniformParameters.getElement(parameterIndex);

		MPlug uniformPlug(param.getPlug());
		if (!uniformPlug.isNull())
		{
			MFnAttribute uniformAttribute(uniformPlug.attribute());
			if (!uniformAttribute.isHidden())
				uniformPlug.setLocked(locked);
		}
	}
}

// ***********************************
// Texture Management
// ***********************************

MHWRender::MTexture* dx11ShaderNode::loadTexture(const MString& textureName, const MString& layerName, int alphaChannelIdx, int mipmapLevels) const
{
	if(textureName.length() == 0)
		return NULL;

	MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
	if(theRenderer == NULL)
		return NULL;

	MHWRender::MTextureManager*	txtManager = theRenderer->getTextureManager();
	if(txtManager == NULL)
		return NULL;

	// check extension of texture.
	// for HDR EXR files, we tell Maya to skip using exposeControl or it would normalize our RGB values via linear mapping
	// We don't want that for things like Vector Displacement Maps.
	// In the future, other 32bit images can be added, such as TIF, but those currently do not load properly in ATIL and
	// therefor we have to force them to use linear exposure control for them to load at all.
	MString extension;
	int idx = textureName.rindexW(L'.');
	if(idx > 0)
	{
		extension = textureName.substringW( idx+1, textureName.length()-1 );
		extension = extension.toLowerCase();
	}
	bool isEXR = (extension == "exr");

	MHWRender::MTexture* texture = txtManager->acquireTexture( textureName, mipmapLevels, !isEXR, layerName, alphaChannelIdx );

#ifdef _DEBUG_SHADER
	if(texture == NULL)
	{
		printf("-- Texture %s not found.\n", textureName.asChar());
	}
#endif

	return texture;
}

void dx11ShaderNode::releaseTexture(MHWRender::MTexture* texture) const
{
	if (texture)
	{
		MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
		if (theRenderer)
		{
			MHWRender::MTextureManager*	txtManager = theRenderer->getTextureManager();
			{
				if (txtManager)
				{
					txtManager->releaseTexture(texture);
				}
			}
		}
	}
}

/*
	Load the texture file and assign to the shader resource variable.

	The texture objects are stored and released when no more used.

	The control between the texture quality and the performance can be modified
	using the kMipmaplevels annotation when declaring the texture in the shader file,
	and by using the kTextureMipmaplevels annotation in the technique declaration.

	kTextureMipmaplevels applies to all the textures, while kMipmaplevels only applies to
	one texture. kMipmaplevels prevails over kTextureMipmaplevels.
*/
void dx11ShaderNode::assignTexture(dx11ShaderDX11EffectShaderResourceVariable* resourceVar, const MString& textureName, const MString& layerName, int alphaChannelIdx, ResourceTextureMap& resourceTexture) const
{
	// When using custom effect (uv editor or even swatch), we use a fixed mipmap levels that reflects the levels set in the orignal effect
	// This is to have consistency in texture quality between uv editor and the scene
	// and also avoid loading a different version of the texture on each draw
	int mipmapLevels = fFixedTextureMipMapLevels;
	if(mipmapLevels < 0)
	{
		// Generate mip map levels desired by technique
		mipmapLevels = fTechniqueTextureMipMapLevels;
		// If the texture itself specify a level, it prevails over the technique's
		getAnnotation(resourceVar, dx11ShaderAnnotation::kMipmaplevels, mipmapLevels);
	}

	MHWRender::MTexture* texture = loadTexture(textureName, layerName, alphaChannelIdx, mipmapLevels);

	ID3D11ShaderResourceView* resource = NULL;
	if(texture != NULL)
	{
		resource = (ID3D11ShaderResourceView*)texture->resourceHandle();
#ifdef _DEBUG_SHADER
		printf("-- Texture activate : new texture %s loaded and bound.\n", textureName.asChar());
#endif
	}

	resourceVar->SetResource( resource );

	// Release the old texture
	ResourceTextureMap::iterator it = resourceTexture.find(resourceVar);
	if(it != resourceTexture.end()) {
		releaseTexture(it->second);
		resourceTexture.erase(it);
	}

	// Register new texture
	if(texture != NULL) {
		resourceTexture[resourceVar] = texture;
	}
}

void dx11ShaderNode::releaseAllTextures(ResourceTextureMap& resourceTexture) const
{
	ResourceTextureMap::iterator it = resourceTexture.begin();
	ResourceTextureMap::iterator itEnd = resourceTexture.end();
	for(; it != itEnd; ++it) {
		releaseTexture(it->second);
	}

	resourceTexture.clear();
}

void dx11ShaderNode::releaseAllTextures()
{
	releaseAllTextures(fResourceTextureMap);
}

/*
	getTextureFile is used to retrieve the path of the texture file linked to the source node when duplicating
*/
bool dx11ShaderNode::getTextureFile(const MString& uniformName, MString& textureFile) const
{
	if(fDuplicateNodeSource)
	{
		// When we are in duplicate command, let the command be in charge of connecting the texture nodes.
		// Leave the textureFile empty so that connection will be created for the texture parameter.
		// Return true so that the builder will not look for a default value from the shader file.
		return true;
	}

	return false;
}

// ***********************************
// Convenient functions
// ***********************************

void dx11ShaderNode::setParameterAsVector(int inParamIndex, float* data) const
{
	if( inParamIndex > -1)
	{
		MUniformParameter uniform = fUniformParameters.getElement(inParamIndex);
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
		if (effectVariable)
			effectVariable->AsVector()->SetFloatVector( data );
	}
}

void dx11ShaderNode::setParameterAsScalar(int inParamIndex, float data) const
{
	if( inParamIndex > -1)
	{
		MUniformParameter uniform = fUniformParameters.getElement(inParamIndex);
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
		if (effectVariable)
			effectVariable->AsScalar()->SetFloat( data );
	}
}

void dx11ShaderNode::setParameterAsScalar(int inParamIndex, bool data) const
{
	if( inParamIndex > -1)
	{
		MUniformParameter uniform = fUniformParameters.getElement(inParamIndex);
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
		if (effectVariable)
			effectVariable->AsScalar()->SetBool( data );
	}
}

void dx11ShaderNode::setParameterAsScalar(int inParamIndex, int data) const
{
	if( inParamIndex > -1)
	{
		MUniformParameter uniform = fUniformParameters.getElement(inParamIndex);
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
		if (effectVariable)
			effectVariable->AsScalar()->SetInt( data );
	}
}

void dx11ShaderNode::setParameterAsMatrix(int inParamIndex, MMatrix& data) const
{
	if( inParamIndex > -1)
	{
		MUniformParameter uniform = fUniformParameters.getElement(inParamIndex);
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
		if (effectVariable)
		{
			float matrix[4][4];
			data.get(matrix);
			effectVariable->AsMatrix()->SetMatrix( (float*)&matrix[0] );
		}
	}
}

void dx11ShaderNode::setParameterAsResource(int inParamIndex, ID3D11ShaderResourceView* inResource) const
{
	if( inParamIndex > -1 && inResource)
	{
		MUniformParameter uniform = fUniformParameters.getElement(inParamIndex);
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
		if (effectVariable)
			effectVariable->AsShaderResource()->SetResource( inResource );
	}
}

void dx11ShaderNode::setParameterFromUniformAsVector(int inParamIndex,const MHWRender::MDrawContext& context, const float *data) const
{
	if( inParamIndex > -1)
	{
		MUniformParameter uniform = fUniformParameters.getElement(inParamIndex);
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
		if (effectVariable)
		{
			if(data == NULL)
				data = uniform.getAsFloatArray(context);

			effectVariable->AsVector()->SetFloatVector( data );
		}
	}
}

void dx11ShaderNode::setParameterFromUniformAsScalar(int inParamIndex,const MHWRender::MDrawContext& context) const
{
	if( inParamIndex > -1)
	{
		MUniformParameter uniform = fUniformParameters.getElement(inParamIndex);
		ID3DX11EffectVariable* effectVariable = (ID3DX11EffectVariable *)uniform.userData();
		if (effectVariable)
			effectVariable->AsScalar()->SetFloat( uniform.getAsFloat(context) );
	}
}



/*
	Parse through the current technique.

	Build and return the current vertexDescList.
*/
const MHWRender::MVertexBufferDescriptorList* dx11ShaderNode::vertexBufferDescLists()
{
	// Test if requirements have changed and rebuild the vertex Desc list if needed
	// ---------------------------------------------------------------
	if (isDirty(fVaryingParametersGeometryVersionId))
	{
		buildVertexDescriptorFromVaryingParameters();
		fVaryingParametersGeometryVersionId = fGeometryVersionId;
	}
	return &fVaryingParametersVertexDescriptorList;
}








// ***********************************
// ERROR Reporting
// ***********************************
void dx11ShaderNode::displayErrorAndWarnings() const
{
	MPlug diagnosticsPlug( thisMObject(), sDiagnostics);
	MString currentDiagnostic;
	diagnosticsPlug.getValue(currentDiagnostic);
	if(fErrorLog.length())
	{
		currentDiagnostic += dx11ShaderStrings::getString( dx11ShaderStrings::kErrorLog, fErrorLog );
		diagnosticsPlug.setValue( currentDiagnostic );

		// If an error occured when loading a scene
		// delay the error message so it is shown last
		// and not lost by the list of missing attributes warnings
		if(MFileIO::isReadingFile() && MFileIO::isOpeningFile())
		{
			AfterOpenErrorCB::addError(fErrorLog);
		}
		else
		{
			MGlobal::displayError(fErrorLog);
		}
		fErrorLog.clear();
	}
	if(fWarningLog.length())
	{
		currentDiagnostic += dx11ShaderStrings::getString( dx11ShaderStrings::kWarningLog, fWarningLog );
		diagnosticsPlug.setValue( currentDiagnostic );
		MGlobal::displayWarning(fWarningLog);
		fWarningLog.clear();
	}
}


#define DX11SHADER_ERROR_LIMIT 20
void dx11ShaderNode::reportInternalError( const char* function, size_t errcode ) const
{
	MString es = "dx11Shader";

	try
	{
		if ( this )
		{
			if ( ++fErrorCount > DX11SHADER_ERROR_LIMIT  )
				return;
			MString s;
			s += "\"";
			s += name();
			s += "\": ";
			s += typeName();
			es = s;
		}
	}
	catch ( ... )
	{}
	es += " internal error ";
	es += (int)errcode;
	es += " in ";
	es += function;
	MGlobal::displayError( es );
} // dx11ShaderNode::reportInternalError

#if defined(MAYA_WANT_EXTERNALCONTENTTABLE)
void dx11ShaderNode::getExternalContent(MExternalContentInfoTable& table) const
{
	addExternalContentForFileAttr(table, sShader);
	MPxHardwareShader::getExternalContent(table);
}

void dx11ShaderNode::setExternalContent(const MExternalContentLocationTable& table)
{
	setExternalContentForFileAttr(sShader, table);
	MPxHardwareShader::setExternalContent(table);
}
#endif
