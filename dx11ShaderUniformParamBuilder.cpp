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

#include "dx11ShaderUniformParamBuilder.h"
#include "dx11Shader.h"
#include "dx11ShaderStrings.h"
#include "dx11ShaderCompileHelper.h"
#include "dx11ShaderSemantics.h"


namespace
{
	// Find a substring, if not found also try for lowercase substring
	int findSubstring(const MString& haystack, const MString& needle)
	{
		int at = haystack.indexW(needle);
		if(at < 0)
		{
			MString needleLowerCase = needle;
			needleLowerCase.toLowerCase();
			at = haystack.indexW(needleLowerCase);
		}
		return at;
	}
}

CUniformParameterBuilder::CUniformParameterBuilder()
	:mEffectVariable(NULL)
	,mShader(NULL)
	,mParamType(eUndefined)
	,mLightType(eNotLight)
	,mLightIndex(-1)
	,mUIGroupIndex(-1)
	,mValidUniformParameter(false)
	,mUIOrder(-1)
	,mAnnotationIndex()
{}

void CUniformParameterBuilder::init(ID3DX11EffectVariable* inEffectVariable,dx11ShaderNode* inShader, int order)
{
	mEffectVariable = inEffectVariable;
	mShader = inShader;
	mUIOrder = order;
	if(mEffectVariable)
	{
		mEffectVariable->GetDesc(&mDesc);
		mEffectVariable->GetType()->GetDesc(&mDescType);
	}
}

/*
	Annotations are constantly evolving. For the latest
	standards, there are two sources of documentation to follow:

	1- MSDN: http://msdn.microsoft.com/en-us/library/windows/desktop/bb206291%28v=vs.85%29.aspx
	         (or search for "DirectX Standard Annotations and Semantics Reference")
	2- NVidia: http://developer.download.nvidia.com/assets/gamedev/docs/Using_SAS_v1_03.pdf

	The code also tries to be compatible with annotations from
	previous versions of the standards.
*/


bool CUniformParameterBuilder::build()
{
	//assert(mEffectVariable);

	// building the uniform based on the type
	// ---------------------------------------
	MUniformParameter::DataType		type		= convertType    ();
	MUniformParameter::DataSemantic semantic	= convertSemantic();

	updateLightInfoFromSemantic();

	// If we don't know what this parameter is, and we've been told to hide it, do so
	// NOTE that for now, we only hide simple constants as hiding everything we're
	// told to hide actually starts hiding textures and things the artist wants to see.
	// ----------------------------------------------------------------------------------
	if (semantic == MUniformParameter::kSemanticUnknown && (type == MUniformParameter::kTypeFloat || type == MUniformParameter::kTypeString))
	{

		if (mDesc.Semantic && _stricmp(mDesc.Semantic, dx11ShaderSemantic::kSTANDARDSGLOBAL)==0)
		{
			return false;
		}
	}

	/*
		The UIGroup annotation is used to groups multiple parameters in a single
		collapsible panel in the attribute editor. This helps organize related
		parameters.

		- All parameters sharing the same UIGroup annotation string will group together
		- The name of the group, as shown as the header of the collapsible panel, will
		   be the value of the annotation
		- The order of the panels in the attribute editor will be sorted according to
			the parameter with the lowest UIOrder in that group
		- UIGroups win over UIOrder, this means all parameters will be grouped in a
			single panel even if there are non grouped parameters that have a UIOrder
			that falls within the range of UIOrders for parameters in that group
		- This commands lists all UIGroups: dx11Shader -listUIGroupInformation
		- This command lists all parameters for a group: dx11Shader -listUIGroupParameters <group>
	*/
	if (mUIGroupIndex == -1)
	{
		LPCSTR pszUIGroupName;
		if( getAnnotation( dx11ShaderAnnotation::kUIGroup, pszUIGroupName) && *pszUIGroupName)
		{
			MString uiGroupName(pszUIGroupName);
			mUIGroupIndex = mShader->getIndexForUIGroupName(uiGroupName, true);
		}
	}

	/*
		The name of the parameter in the attribute editor defaults to
		the name of the variable associated with the parameter. If there
		is a UIName attribute on the parameter, and the 'kVariableNameAsAttributeName'
		semantic is not set, this name will be used to
		define all three of the parameter short/long/nice name. If the
		UIName contains spaces or other script unfriendly characters,
		those will be replaced by underscores in the short and long names
		used in scripting.

		Using UIName as attribute name can lead to ambiguity since UIName annotations 
		are not required to be unique by the compiler. The MPxHardwareShader class
		will add numbers at the end of the short/long names as required to
		make them unique.
	*/
	LPCSTR paramName = NULL;

	bool varAsAttr = false;
	if (mShader)
		varAsAttr = mShader->getVariableNameAsAttributeName();

	LPCSTR uiName = NULL ;
	bool hasUIName = getAnnotation( dx11ShaderAnnotation::kUIName, uiName);

	if( varAsAttr || !hasUIName )
		paramName = mDesc.Name;
	else
		paramName = uiName;

	/*
		If an integer parameter has a UIFieldNames annotation, then the
		annotation contents will be used to populate a dropdown menu when the
		parameter is shown in the attribute editor. The parameter type is
		changed from integer to enum.
	*/
	LPCSTR uiFieldNames = NULL ;
	MString fieldNames;
	if(type == MUniformParameter::kTypeInt && getAnnotation(dx11ShaderAnnotation::kUIFieldNames,uiFieldNames) && uiFieldNames)
	{
		fieldNames = uiFieldNames;
		type = MUniformParameter::kTypeEnum;
	}

	MUniformParameter uniform( paramName, type, semantic, mDescType.Rows, mDescType.Columns, (void*)mEffectVariable);
	mParam = uniform;

	// If shader author has specified to use var as attribute name and provided a UIName, then
	// tell Maya to use the UIName for the attribute's 'Nice Name':
	if (varAsAttr && hasUIName)
		mParam.setUINiceName( uiName );

	updateRangeFromAnnotation();
	updateUIVisibilityFromAnnotation();
	if (fieldNames.length())
	{
		mParam.setEnumFieldNames(fieldNames);
	}

	/*
		The UIOrder annotation can be used to make sure all parameters
		appear in a predictable sequence in the attribute editors even
		if the compiler decides to reorder them is the input block.

		- The content of the annotation is an integer
		- Parameters will be sorted according to increasing values of
		  UIOrder annotation
		- The sort is stable, so parameters with identical UIOrder will
		  appear in the order the compiler outputs them (which is not as
		  stable as the sort)
	*/
	getAnnotation(dx11ShaderAnnotation::kUIOrder, mUIOrder);

	// set keyable for visible attributes other than textures
	mParam.setKeyable(!mParam.UIHidden() && !mParam.isATexture());

	bool result = setParameterValueFromEffect();
	if(result)
	{
		mValidUniformParameter = true;
	}

	return result;
}

/*
	This function tests for any annotation that can be used to
	control the visibility of a parameter:
*/
void CUniformParameterBuilder::updateUIVisibilityFromAnnotation()
{
	BOOL visible = TRUE;
	if( getBOOLAnnotation( dx11ShaderAnnotation::kSasUiVisible, visible))
	{
		mParam.setUIHidden(!visible);
	}
	else
	{
		LPCSTR uiTypeValue = NULL;
		bool  foundUIType = getAnnotation( dx11ShaderAnnotation::kUIType,uiTypeValue);
		if (foundUIType && !_stricmp(uiTypeValue, dx11ShaderAnnotationValue::kNone))
		{
			mParam.setUIHidden(true);
		}
		// As per NVidia SAS docs v1.0.3:
		foundUIType = getAnnotation( dx11ShaderAnnotation::kUIWidget,uiTypeValue);
		if (foundUIType && !_stricmp(uiTypeValue, dx11ShaderAnnotationValue::kNone))
		{
			mParam.setUIHidden(true);
		}
	}
}

/*
	These annotations can be used to control the numeric range of
	a slider. The hard min/max are defined as per documented standards,
	and we introduced the soft min/max notion to match the normal
	behavior of Maya attributes. A slider range will be limited to
	values between min and max, but the initial slider displayed in the
	UI will go from soft min to soft max.
*/
void CUniformParameterBuilder::updateRangeFromAnnotation()
{
	switch(mParam.type())
	{
	case MUniformParameter::kTypeFloat:
	case MUniformParameter::kTypeInt:
		{
			float uiMinFloat = NULL;
			if(getAnnotation( dx11ShaderAnnotation::kSasUiMin,uiMinFloat) ||
			   getAnnotation( dx11ShaderAnnotation::kUIMin,uiMinFloat) ||
			   getAnnotation( dx11ShaderAnnotation::kuimin,uiMinFloat) )
			{
				mParam.setRangeMin(uiMinFloat);
			}

			float uiMaxFloat = NULL;
			if(getAnnotation( dx11ShaderAnnotation::kSasUiMax,uiMaxFloat) ||
			   getAnnotation( dx11ShaderAnnotation::kUIMax,uiMaxFloat) ||
			   getAnnotation( dx11ShaderAnnotation::kuimax,uiMaxFloat) )
			{
				mParam.setRangeMax(uiMaxFloat);
			}

			float uiSoftMinFloat = NULL;
			if(getAnnotation( dx11ShaderAnnotation::kSasUiSoftMin,uiSoftMinFloat) ||
			   getAnnotation( dx11ShaderAnnotation::kUISoftMin,uiSoftMinFloat) ||
			   getAnnotation( dx11ShaderAnnotation::kuisoftmin,uiSoftMinFloat) )
			{
				mParam.setSoftRangeMin(uiSoftMinFloat);
			}

			float uiSoftMaxFloat = NULL;
			if(getAnnotation( dx11ShaderAnnotation::kSasUiSoftMax,uiSoftMaxFloat) ||
			   getAnnotation( dx11ShaderAnnotation::kUISoftMax,uiSoftMaxFloat) ||
			   getAnnotation( dx11ShaderAnnotation::kuisoftmax,uiSoftMaxFloat) )
			{
				mParam.setSoftRangeMax(uiSoftMaxFloat);
			}
		}
		break;
	default:
		break;
	};

}

/*
	Here we retrieve the initial value for an effect parameter.
	This value will be set as the default value of the corresponding
	uniform parameter attribute and can be used to test if the
	current value of a parameter is different from the default or
	not.
*/
bool CUniformParameterBuilder::setParameterValueFromEffect()
{
	int length = getLength();
	MUniformParameter::DataType type = mParam.type();
	switch( type )
	{
	case MUniformParameter::kTypeFloat:
		{
			std::vector<FLOAT> values;
			values.resize(length);
			FLOAT* Value = &values[0];
			switch(mDescType.Class)
			{
			case D3D10_SVC_SCALAR:
				mEffectVariable->AsScalar()->GetFloat( Value );
				break;
			case D3D10_SVC_VECTOR:
				mEffectVariable->AsVector()->GetFloatVector( Value );
				break;
			case D3D10_SVC_MATRIX_COLUMNS:
				mEffectVariable->AsMatrix()->GetMatrix( Value );
				break;
			case D3D10_SVC_MATRIX_ROWS:
				mEffectVariable->AsMatrix()->GetMatrixTranspose( Value );
				break;
			default:
				return false;
			};

			mParam.setAsFloatArray( Value, length);
		}
		break;
	case MUniformParameter::kTypeString:
		{
			if (length==1)
			{
				LPCSTR Value;
				if( mEffectVariable->AsString()->GetString( &Value) == S_OK)
				{
					mParam.setAsString( MString( Value));
				}
			}
			else
			{
				logUnsupportedTypeWarning(dx11ShaderStrings::kTypeStringVector);
				return false;
			}
		}
		break;
	case MUniformParameter::kTypeBool:
		{
			if (length==1)
			{
#if _MSC_VER < 1700
				BOOL Value;
#else
				bool Value;
#endif
				if( mEffectVariable->AsScalar()->GetBool( &Value) == S_OK)
				{
					mParam.setAsBool( Value ? true : false);
				}
			}
			else
			{
				logUnsupportedTypeWarning(dx11ShaderStrings::kTypeBoolVector);
				return false;
			}
		}
		break;
	case MUniformParameter::kTypeInt:
	case MUniformParameter::kTypeEnum:
		{
			if (length==1)
			{
				INT Value;
				if( mEffectVariable->AsScalar()->GetInt( &Value) == S_OK)
				{
					mParam.setAsInt( Value);
				}
			}
			else
			{
				logUnsupportedTypeWarning(dx11ShaderStrings::kTypeIntVector);
				return false;
			}
		}
		break;
	default:
		if( type >= MUniformParameter::kType1DTexture && type <= MUniformParameter::kTypeEnvTexture)
		{
			// We have a texture name but no resource view. Try and load in
			// a new texture and get a new resource view.
			MString textureFile;
			LPCSTR resource;
			MString paramName = mParam.name();
			if( mShader->getTextureFile( paramName, textureFile ) )
			{
				mParam.setAsString( textureFile );
			}
			else if( getAnnotation( dx11ShaderAnnotation::kResourceName, resource) && *resource)
			{
				mParam.setAsString( mShader->findResource( MString( resource), CDX11EffectCompileHelper::resolveShaderFileName(mShader->effectName()) ));
			}
			else if( getAnnotation( dx11ShaderAnnotation::kSasResourceAddress, resource) && *resource)
			{
				mParam.setAsString( mShader->findResource( MString( resource), CDX11EffectCompileHelper::resolveShaderFileName(mShader->effectName()) ));
			}
		}
		else
		{
			return false;
		}
	}

	return true;
}

MUniformParameter::DataType CUniformParameterBuilder::convertType()
{
	MUniformParameter::DataType		paramType = MUniformParameter::kTypeUnknown;

	switch( mDescType.Type) {

	case D3D10_SVT_BOOL:			paramType = MUniformParameter::kTypeBool;	break;
	case D3D10_SVT_INT:				paramType = MUniformParameter::kTypeInt;	break;
	case D3D10_SVT_FLOAT:			paramType = MUniformParameter::kTypeFloat;	break;
	case D3D10_SVT_STRING:			paramType = MUniformParameter::kTypeString;	break;
	case D3D10_SVT_UINT:			paramType = MUniformParameter::kTypeInt;	break;

	case D3D10_SVT_UINT8:
		logUnsupportedTypeWarning(dx11ShaderStrings::kTypeIntUInt8);
		break;
	case D3D11_SVT_DOUBLE:
		logUnsupportedTypeWarning(dx11ShaderStrings::kTypeDouble);
		break;

	case D3D11_SVT_RWTEXTURE1D:
	case D3D10_SVT_TEXTURE1D:			paramType = MUniformParameter::kType1DTexture;		break;
	case D3D11_SVT_RWTEXTURE2D:
	case D3D10_SVT_TEXTURE2D:			paramType = MUniformParameter::kType2DTexture;		break;
	case D3D11_SVT_RWTEXTURE3D:
	case D3D10_SVT_TEXTURE3D:			paramType = MUniformParameter::kType3DTexture;		break;
	case D3D10_SVT_TEXTURECUBE:			paramType = MUniformParameter::kTypeCubeTexture;	break;
	case D3D10_SVT_TEXTURE:
		{
			// The shader hasn't used a typed texture, so first see if there's an annotation
			// that tells us which type to use.
			// -------------------------------------
			LPCSTR	textureType;
			if( ( getAnnotation( dx11ShaderAnnotation::kTextureType, textureType) || getAnnotation( dx11ShaderAnnotation::kResourceType, textureType)) && textureType) {
				// Grab the type off from the annotation
				// -------------------------------------
				if( !_stricmp( textureType, dx11ShaderAnnotationValue::k1D)) paramType = MUniformParameter::kType1DTexture;
				else if( !_stricmp( textureType, dx11ShaderAnnotationValue::k2D)) paramType = MUniformParameter::kType2DTexture;
				else if( !_stricmp( textureType, dx11ShaderAnnotationValue::k3D)) paramType = MUniformParameter::kType3DTexture;
				else if( !_stricmp( textureType, dx11ShaderAnnotationValue::kCube)) paramType = MUniformParameter::kTypeCubeTexture;
				else {
					MStringArray args;
					args.append(textureType);
					args.append(mDesc.Name);

					mWarnings += dx11ShaderStrings::getString( dx11ShaderStrings::kUnknownTextureSemantic, args );
				}
			}

			if( paramType == MUniformParameter::kTypeUnknown) {
				// No explicit type. At this stage, it would be nice to take a look at the
				// sampler which uses the texture and grab the type of that, but I can't see
				// any way to query for the sampler -> texture bindings through the effect
				// API.
				//
				paramType = MUniformParameter::kType2DTexture;
				mWarnings += dx11ShaderStrings::getString( dx11ShaderStrings::kNoTextureType, mDesc.Name );
			}

			break;

		}
	case D3D10_SVT_TEXTURE1DARRAY:
	case D3D10_SVT_TEXTURE2DARRAY:
	case D3D10_SVT_TEXTURE2DMS:
	case D3D10_SVT_TEXTURE2DMSARRAY:
	case D3D10_SVT_TEXTURECUBEARRAY:
	case D3D11_SVT_RWTEXTURE1DARRAY:
	case D3D11_SVT_RWTEXTURE2DARRAY:
		logUnsupportedTypeWarning(dx11ShaderStrings::kTypeTextureArray);
		break;

	case D3D10_SVT_VOID:
	case D3D10_SVT_SAMPLER:
	case D3D10_SVT_PIXELSHADER:
	case D3D10_SVT_VERTEXSHADER:
	case D3D10_SVT_GEOMETRYSHADER:
	case D3D10_SVT_RASTERIZER:
	case D3D10_SVT_DEPTHSTENCIL:
	case D3D10_SVT_BLEND:
	case D3D10_SVT_BUFFER:
	case D3D10_SVT_CBUFFER:
	case D3D10_SVT_TBUFFER:
	case D3D11_SVT_HULLSHADER:
	case D3D11_SVT_DOMAINSHADER:
	case D3D11_SVT_INTERFACE_POINTER:
	case D3D11_SVT_COMPUTESHADER:
	case D3D11_SVT_BYTEADDRESS_BUFFER:
	case D3D11_SVT_RWBYTEADDRESS_BUFFER:
	case D3D11_SVT_STRUCTURED_BUFFER:
	case D3D11_SVT_RWSTRUCTURED_BUFFER:
	case D3D11_SVT_APPEND_STRUCTURED_BUFFER:
	case D3D11_SVT_CONSUME_STRUCTURED_BUFFER:
	case D3D10_SVT_RENDERTARGETVIEW:
	case D3D10_SVT_DEPTHSTENCILVIEW:
	case D3D11_SVT_RWBUFFER:
		// ignored variable definitions
		// ------------------------------
		break;

	default:
		break;
	}
	return paramType;
}

//
// Convert a DX space into a Maya space
//
MUniformParameter::DataSemantic CUniformParameterBuilder::convertSpace( MUniformParameter::DataSemantic defaultSpace)
{
	MUniformParameter::DataSemantic space = MUniformParameter::kSemanticUnknown;

	LPCSTR ann;
	if( getAnnotation( dx11ShaderAnnotation::kSpace, ann) && ann)
	{
		if( !_stricmp( ann, dx11ShaderAnnotationValue::kObject))		space = defaultSpace >= MUniformParameter::kSemanticObjectPos ? MUniformParameter::kSemanticObjectPos	: MUniformParameter::kSemanticObjectDir;
		else if( !_stricmp( ann, dx11ShaderAnnotationValue::kWorld))	space = defaultSpace >= MUniformParameter::kSemanticObjectPos ? MUniformParameter::kSemanticWorldPos	: MUniformParameter::kSemanticWorldDir;
		else if( !_stricmp( ann, dx11ShaderAnnotationValue::kView))		space = defaultSpace >= MUniformParameter::kSemanticObjectPos ? MUniformParameter::kSemanticViewPos		: MUniformParameter::kSemanticViewDir;
		else if( !_stricmp( ann, dx11ShaderAnnotationValue::kCamera))	space = defaultSpace >= MUniformParameter::kSemanticObjectPos ? MUniformParameter::kSemanticViewPos		: MUniformParameter::kSemanticViewDir;
		else
		{
			MStringArray args;
			args.append(ann);
			args.append(mDesc.Name);

			mWarnings += dx11ShaderStrings::getString( dx11ShaderStrings::kUnknowSpace, args );
		}
	}

	return space;
}

//
// Convert a DX semantic into a Maya semantic
//
MUniformParameter::DataSemantic CUniformParameterBuilder::convertSemantic( )
{
	MUniformParameter::DataSemantic paramSemantic = MUniformParameter::kSemanticUnknown;


	// @@@@ maybe useful to validate
	// MHWRender::MShaderManager::isSupportedShaderSemantic( semantic );
	// Convert all semantics to lower case for faster
	// mtype = MHWRender::MFrameContext::semanticToMatrixType( semantic, &status );

	// First try the explicit semantic
	if( mDesc.Semantic)
	{
		if(	     !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorld))								paramSemantic = MUniformParameter::kSemanticWorldMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldTranspose))						paramSemantic = MUniformParameter::kSemanticWorldTransposeMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldInverse))							paramSemantic = MUniformParameter::kSemanticWorldInverseMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldInverseTranspose))				paramSemantic = MUniformParameter::kSemanticWorldInverseTransposeMatrix;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kView))									paramSemantic = MUniformParameter::kSemanticViewMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewTranspose))						paramSemantic = MUniformParameter::kSemanticViewTransposeMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewInverse))							paramSemantic = MUniformParameter::kSemanticViewInverseMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewInverseTranspose))					paramSemantic = MUniformParameter::kSemanticViewInverseTransposeMatrix;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kProjection))							paramSemantic = MUniformParameter::kSemanticProjectionMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kProjectionTranspose))					paramSemantic = MUniformParameter::kSemanticProjectionTransposeMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kProjectionInverse))					paramSemantic = MUniformParameter::kSemanticProjectionInverseMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kProjectionInverseTranspose))			paramSemantic = MUniformParameter::kSemanticProjectionInverseTransposeMatrix;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldView))							paramSemantic = MUniformParameter::kSemanticWorldViewMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldViewTranspose))					paramSemantic = MUniformParameter::kSemanticWorldViewTransposeMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldViewInverse))						paramSemantic = MUniformParameter::kSemanticWorldViewInverseMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldViewInverseTranspose))			paramSemantic = MUniformParameter::kSemanticWorldViewInverseTransposeMatrix;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewProjection))						paramSemantic = MUniformParameter::kSemanticViewProjectionMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewProjectionTranspose))				paramSemantic = MUniformParameter::kSemanticViewProjectionTransposeMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewProjectionInverse))				paramSemantic = MUniformParameter::kSemanticViewProjectionInverseMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewProjectionInverseTranspose))		paramSemantic = MUniformParameter::kSemanticViewProjectionInverseTransposeMatrix;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldViewProjection))					paramSemantic = MUniformParameter::kSemanticWorldViewProjectionMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldViewProjectionTranspose))			paramSemantic = MUniformParameter::kSemanticWorldViewProjectionTransposeMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldViewProjectionInverse))			paramSemantic = MUniformParameter::kSemanticWorldViewProjectionInverseMatrix;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kWorldViewProjectionInverseTranspose))	paramSemantic = MUniformParameter::kSemanticWorldViewProjectionInverseTransposeMatrix;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewDirection))						paramSemantic = MUniformParameter::kSemanticViewDir;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewPosition))							paramSemantic = MUniformParameter::kSemanticViewPos;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLocalViewer))							paramSemantic = MUniformParameter::kSemanticLocalViewer;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kViewportPixelSize))					paramSemantic = MUniformParameter::kSemanticViewportPixelSize;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kBackgroundColor))						paramSemantic = MUniformParameter::kSemanticBackgroundColor;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kFrame))								paramSemantic = MUniformParameter::kSemanticFrameNumber;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kFrameNumber))							paramSemantic = MUniformParameter::kSemanticFrameNumber;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAnimationTime))						paramSemantic = MUniformParameter::kSemanticTime;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kTime))									paramSemantic = MUniformParameter::kSemanticTime;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kColor))								paramSemantic = MUniformParameter::kSemanticColor;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightColor))							paramSemantic = MUniformParameter::kSemanticColor;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAmbient))								paramSemantic = MUniformParameter::kSemanticColor;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightAmbientColor))					paramSemantic = MUniformParameter::kSemanticColor;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kSpecular))								paramSemantic = MUniformParameter::kSemanticColor;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightSpecularColor))					paramSemantic = MUniformParameter::kSemanticColor;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kDiffuse))								paramSemantic = MUniformParameter::kSemanticColor;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kNormal))								paramSemantic = MUniformParameter::kSemanticNormal;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kBump))									paramSemantic = MUniformParameter::kSemanticBump;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kEnvironment))							paramSemantic = MUniformParameter::kSemanticEnvironment;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kPosition))								paramSemantic = convertSpace( MUniformParameter::kSemanticWorldPos);
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAreaPosition0))						paramSemantic = convertSpace( MUniformParameter::kSemanticWorldPos);
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAreaPosition1))						paramSemantic = convertSpace( MUniformParameter::kSemanticWorldPos);
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAreaPosition2))						paramSemantic = convertSpace( MUniformParameter::kSemanticWorldPos);
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAreaPosition3))						paramSemantic = convertSpace( MUniformParameter::kSemanticWorldPos);
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kDirection))							paramSemantic = convertSpace( MUniformParameter::kSemanticViewDir);

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowMap))							paramSemantic = MUniformParameter::kSemanticColorTexture;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowColor))							paramSemantic = MUniformParameter::kSemanticColor;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowFlag))							paramSemantic = MUniformParameter::kSemanticUnknown;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowMapBias))						paramSemantic = MUniformParameter::kSemanticUnknown;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowMapMatrix))						paramSemantic = MUniformParameter::kSemanticUnknown;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowMapXForm))						paramSemantic = MUniformParameter::kSemanticUnknown;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kStandardsGlobal))						paramSemantic = MUniformParameter::kSemanticUnknown;

		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kTranspDepthTexture))					paramSemantic = MUniformParameter::kSemanticTranspDepthTexture;
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kOpaqueDepthTexture))					paramSemantic = MUniformParameter::kSemanticOpaqueDepthTexture;

		else
		{
			logUnrecognisedSemantic(mDesc.Semantic);
		}
	}

	// Next, try annotation semantic
	if( paramSemantic == MUniformParameter::kSemanticUnknown)
	{
		LPCSTR sasSemantic;
		if( getAnnotation( dx11ShaderAnnotation::kSasBindAddress, sasSemantic) && sasSemantic && *sasSemantic)
		{
			MString str( sasSemantic);
			if(      !_stricmp( sasSemantic, dx11ShaderAnnotationValue::kSas_Skeleton_MeshToJointToWorld_0_))	paramSemantic = MUniformParameter::kSemanticWorldMatrix;
			else if( !_stricmp( sasSemantic, dx11ShaderAnnotationValue::kSas_Camera_WorldToView))				paramSemantic = MUniformParameter::kSemanticViewMatrix;
			else if( !_stricmp( sasSemantic, dx11ShaderAnnotationValue::kSas_Camera_Projection))				paramSemantic = MUniformParameter::kSemanticProjectionMatrix;
			else if( !_stricmp( sasSemantic, dx11ShaderAnnotationValue::kSas_Time_Now))							paramSemantic = MUniformParameter::kSemanticTime;
			else if( str.rindexW( dx11ShaderAnnotationValue::k_Position) >= 0)									paramSemantic = convertSpace( MUniformParameter::kSemanticWorldPos);
			else if( str.rindexW( dx11ShaderAnnotationValue::k_Direction) >= 0 &&
					 str.rindexW( dx11ShaderAnnotationValue::k_Direction) != str.rindexW( dx11ShaderAnnotationValue::k_Directional))	paramSemantic = convertSpace( MUniformParameter::kSemanticViewDir);
			else
			{
				logUnrecognisedSemantic(sasSemantic);
			}
		}
	}

	// Next try control type
	if( paramSemantic == MUniformParameter::kSemanticUnknown)
	{
		LPCSTR sasSemantic;
		if( (getAnnotation( dx11ShaderAnnotation::kSasUiControl, sasSemantic) || getAnnotation( dx11ShaderAnnotation::kUIWidget, sasSemantic)) && *sasSemantic)
		{
			if( !_stricmp( sasSemantic, dx11ShaderAnnotationValue::kColorPicker))							paramSemantic = MUniformParameter::kSemanticColor;
		}
	}

	// As a last ditch effort, look for an obvious parameter name
	if( paramSemantic == MUniformParameter::kSemanticUnknown && !mDesc.Semantic &&
		mDescType.Class == D3D_SVC_VECTOR && (mDescType.Type == D3D_SVT_FLOAT || mDescType.Type == D3D_SVT_DOUBLE) &&
		mDescType.Rows == 1 && mDescType.Columns >= 3 && mDescType.Columns <= 4 )
	{
		MString name( mDesc.Name);
		if(      name.rindexW( dx11ShaderAnnotationValue::kPosition) >= 0)								paramSemantic = convertSpace( MUniformParameter::kSemanticWorldPos);
		else if( name.rindexW( dx11ShaderAnnotationValue::kDirection) >= 0 &&
				 name.rindexW( dx11ShaderAnnotationValue::kDirection) != name.rindexW( dx11ShaderAnnotationValue::kDirectional))	paramSemantic = convertSpace(  MUniformParameter::kSemanticWorldDir);
		else if( name.rindexW( dx11ShaderAnnotationValue::kColor) >= 0 ||
				 name.rindexW( dx11ShaderAnnotationValue::kColour) >= 0 ||
				 name.rindexW( dx11ShaderAnnotationValue::kDiffuse) >= 0 ||
				 name.rindexW( dx11ShaderAnnotationValue::kSpecular) >= 0 ||
				 name.rindexW( dx11ShaderAnnotationValue::kAmbient) >= 0)								paramSemantic = MUniformParameter::kSemanticColor;
	}

	return paramSemantic;
}

/*
	Here we find light specific semantics on parameters. This will be used
	to properly transfer values from a Maya light to the effect. Parameters
	that have semantics that are not light-like will get the light type
	eNotALight and will not participate in light related code paths.

	We also try to detect the light type that best match this parameter based
	on a substring match for point/spot/directional/ambient strings. We can also
	deduce the light type from extremely specialized semantics like cone angle and
	falloff for a spot light or LP0 for an area light.

	We finally try to group light parameters together into a single logical light
	group using either an "Object" annotation or a substring of the parameter name.

	The light group name is one of:
		- The string value of the "Object" annotation
		- The prefix part of a parameter name that contains either "Light", "light",
		   or a number:
				DirectionalLightColor  ->   DirectionalLight
				scene_light_position   ->   scene_light
				Lamp0Color             ->   Lamp0

	- All light parameters that share a common light group name will be grouped together
		into a single logical light
	- When a logical light is bound to a scene light, all parameter values will be
		transferred in block from the scene light to the logical light
	- The Attribute Editor will show one extra control per logical light that will allow
		to quickly specify how this logical light should be handled by Maya. Options are
		to explicitely bind a scene light, allow automatic binding to any compatible scene
		light, or ignore scene lights and use values stored in the effect parameters.
	- The Attribute Editor will also group all light parameters in separate panels as if
		they were grouped using the UIGroup annotation. See comments on UIGroup annotation
		for more details.
*/
void CUniformParameterBuilder::updateLightInfoFromSemantic()
{
	//Check for light type from object type
	LPCSTR objectType;
	if( getAnnotation( dx11ShaderAnnotation::kObject, objectType) && *objectType)
	{
		MString objectAnnotation(objectType);
		mLightIndex = mShader->getIndexForLightName(objectAnnotation, true);
		mUIGroupIndex = mShader->getIndexForUIGroupName(objectAnnotation, true);
		if(objectAnnotation.rindexW(dx11ShaderAnnotationValue::kLight) >= 0 || objectAnnotation.rindexW(dx11ShaderAnnotationValue::kLamp) >= 0)
		{
			mLightType = eUndefinedLight;
			if(objectAnnotation.rindexW(dx11ShaderAnnotationValue::kPoint) >= 0)
			{
				mLightType = ePointLight;
			}
			else if(objectAnnotation.rindexW(dx11ShaderAnnotationValue::kSpot) >= 0)
			{
				mLightType = eSpotLight;
			}
			else if(objectAnnotation.rindexW(dx11ShaderAnnotationValue::kDirectional) >= 0)
			{
				mLightType = eDirectionalLight;
			}
			else if(objectAnnotation.rindexW(dx11ShaderAnnotationValue::kAmbient) >= 0)
			{
				mLightType = eAmbientLight;
			}
		}
	}

	if( mDesc.Semantic)
	{
		MString name( mDesc.Name);

		if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightColor))
		{
			mParamType = eLightColor;
		}
		if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightEnable))
		{
			mParamType = eLightEnable;
		}
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightIntensity))
		{
			mParamType = eLightIntensity;
		}
		else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightFalloff) ||
			     !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kFalloff))
		{
			mLightType = eSpotLight;
			mParamType = eLightFalloff;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightDiffuseColor))
		{
			mParamType = eLightDiffuseColor;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightAmbientColor))
		{
			mParamType = eLightAmbientColor;
			mLightType = eAmbientLight;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightSpecularColor))
		{
			mParamType = eLightSpecularColor;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowMap))
		{
			mParamType = eLightShadowMap;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowMapBias))
		{
			mParamType = eLightShadowMapBias;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowFlag))
		{
			mParamType = eLightShadowOn;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowMapMatrix) ||
			     !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowMapXForm))
		{
			//View transformation matrix of the light
			mParamType = eLightShadowViewProj;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kShadowColor))
		{
			mParamType = eLightShadowColor;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kHotspot))
		{
			mParamType = eLightHotspot;
			mLightType = eSpotLight;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kLightType))
		{
			mParamType = eLightType;
		}
		else if (!_stricmp( mDesc.Semantic, dx11ShaderSemantic::kDecayRate))
		{
			mParamType = eDecayRate;
		}
		else
		{
			bool isLight = (mLightType != eNotLight || findSubstring(name, MString(dx11ShaderAnnotationValue::kLight)) >= 0);
			if(isLight)
			{
				if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kPosition))
				{
					mParamType = eLightPosition;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAreaPosition0))
				{
					mParamType = eLightAreaPosition0;
					mLightType = eAreaLight;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAreaPosition1))
				{
					mParamType = eLightAreaPosition1;
					mLightType = eAreaLight;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAreaPosition2))
				{
					mParamType = eLightAreaPosition2;
					mLightType = eAreaLight;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAreaPosition3))
				{
					mParamType = eLightAreaPosition3;
					mLightType = eAreaLight;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kDirection))
				{
					mParamType = eLightDirection;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kColor))
				{
					//
					mParamType = eLightColor;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kAmbient))
				{
					mParamType = eLightAmbientColor;
					mLightType = eAmbientLight;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kDiffuse))
				{
					mParamType = eLightDiffuseColor;
				}
				else if( !_stricmp( mDesc.Semantic, dx11ShaderSemantic::kSpecular))
				{
					mParamType = eLightSpecularColor;
				}
			}
		}

		//Compute light index
		if(mParamType != eUndefined && mLightIndex ==  -1)
		{
			//Check object semantic for index
			//Check name for index
			if(mLightType == eNotLight)
			{
				mLightType = eUndefinedLight;
			}

			const char* objectName = name.asChar();
			int truncationPos = -1;

			int lightPos = findSubstring(name, MString(dx11ShaderAnnotationValue::kLight));
			if (lightPos >= 0)
				truncationPos = lightPos + 5;

			if(truncationPos < 0)
			{
				// last effort, see if there is any digit in the parameter name:
				unsigned int digitPos = 0;
				for ( ; digitPos < name.numChars(); ++digitPos)
					if ( isdigit(objectName[digitPos]) )
						break;
				if ( digitPos < name.numChars() )
					truncationPos = digitPos;
			}
			if (truncationPos >= 0)
			{
				// Need to also skip any digits found after the "light"
				int maxChars = int(name.numChars());
				while (truncationPos < maxChars && isdigit(objectName[truncationPos]))
					truncationPos++;

				mLightIndex = mShader->getIndexForLightName(name.substring(0,truncationPos-1), true);
				mUIGroupIndex = mShader->getIndexForUIGroupName(name.substring(0,truncationPos-1), true);
			}
		}

	}
}

// Get semantic string back from enum:
MString& CUniformParameterBuilder::getLightParameterSemantic(int lightParameterType) {

	if (lightParameterType < 0 || lightParameterType >= eLastParameterType)
		lightParameterType = eUndefined;

	static MStringArray semanticNames;

	if (!semanticNames.length()) {
		semanticNames.append(dx11ShaderSemantic::kUndefined);
		semanticNames.append(dx11ShaderSemantic::kPosition);
		semanticNames.append(dx11ShaderSemantic::kDirection);
		semanticNames.append(dx11ShaderSemantic::kLightColor);
		semanticNames.append(dx11ShaderSemantic::kLightSpecularColor);
		semanticNames.append(dx11ShaderSemantic::kLightAmbientColor);
		semanticNames.append(dx11ShaderSemantic::kLightDiffuseColor);
		semanticNames.append(dx11ShaderSemantic::kLightRange);          // Not recognized!
		semanticNames.append(dx11ShaderSemantic::kFalloff);
		semanticNames.append(dx11ShaderSemantic::kLightAttenuation0);   // Not recognized!
		semanticNames.append(dx11ShaderSemantic::kLightAttenuation1);   // Not recognized!
		semanticNames.append(dx11ShaderSemantic::kLightAttenuation2);   // Not recognized!
		semanticNames.append(dx11ShaderSemantic::kLightTheta);   // Not recognized!
		semanticNames.append(dx11ShaderSemantic::kLightPhi);   // Not recognized!
		semanticNames.append(dx11ShaderSemantic::kShadowMap);
		semanticNames.append(dx11ShaderSemantic::kShadowMapBias);
		semanticNames.append(dx11ShaderSemantic::kShadowColor);
		semanticNames.append(dx11ShaderSemantic::kShadowMapMatrix);
		semanticNames.append(dx11ShaderSemantic::kShadowFlag);
		semanticNames.append(dx11ShaderSemantic::kLightIntensity);
		semanticNames.append(dx11ShaderSemantic::kHotspot);
		semanticNames.append(dx11ShaderSemantic::kLightEnable);
		semanticNames.append(dx11ShaderSemantic::kLightType);
		semanticNames.append(dx11ShaderSemantic::kDecayRate);
		semanticNames.append(dx11ShaderSemantic::kAreaPosition0);
		semanticNames.append(dx11ShaderSemantic::kAreaPosition1);
		semanticNames.append(dx11ShaderSemantic::kAreaPosition2);
		semanticNames.append(dx11ShaderSemantic::kAreaPosition3);
	}
	return semanticNames[lightParameterType];
}

ID3DX11EffectVariable* CUniformParameterBuilder::findAnnotationByName(ID3DX11EffectVariable *effectVariable, const char* name)
{
	// The latest effect 11 library is very verbose when an annotation
	// is not found by name. This version will stay quiet if the
	// annotation is not found.
	if (mAnnotationIndex.empty())
	{
		D3DX11_EFFECT_VARIABLE_DESC varDesc;
		effectVariable->GetDesc(&varDesc);
		for (uint32_t idx = 0; idx < varDesc.Annotations; ++idx)
		{
			ID3DX11EffectVariable* var = effectVariable->GetAnnotationByIndex(idx);
			if (var)
			{
				D3DX11_EFFECT_VARIABLE_DESC varDesc;
				var->GetDesc(&varDesc);
				mAnnotationIndex.insert(TAnnotationIndex::value_type(std::string(varDesc.Name), idx));
			}
		}

		mAnnotationIndex.insert(TAnnotationIndex::value_type(std::string("Done!"), 0));
	}

	TAnnotationIndex::const_iterator index = mAnnotationIndex.find(std::string(name));
	if (index != mAnnotationIndex.end())
		return effectVariable->GetAnnotationByIndex((*index).second);
	else
		return 0;
}

bool CUniformParameterBuilder::getAnnotation(const char* name,LPCSTR& value)
{
	ID3DX11EffectVariable* annotationVariable= findAnnotationByName(mEffectVariable, name);
	if (annotationVariable) {
		if (annotationVariable->AsString()->GetString(&value)==S_OK) {
			return true;
		}
	}
	return false;
}

bool CUniformParameterBuilder::getAnnotation(const char* name,float& value)
{
	ID3DX11EffectVariable* annotationVariable= findAnnotationByName(mEffectVariable, name);
	if (annotationVariable) {
		if (annotationVariable->AsScalar()->GetFloat(&value)==S_OK) {
			return true;
		}
	}
	return false;
}

bool CUniformParameterBuilder::getAnnotation(const char* name,int& value)
{
	ID3DX11EffectVariable* annotationVariable= findAnnotationByName(mEffectVariable, name);
	if (annotationVariable) {
		if (annotationVariable->AsScalar()->GetInt(&value)==S_OK) {
			return true;
		}
	}
	return false;
}

bool CUniformParameterBuilder::getBOOLAnnotation(const char* name,BOOL& value)
{
	ID3DX11EffectVariable* annotationVariable= findAnnotationByName(mEffectVariable, name);
	if (annotationVariable) {
#if _MSC_VER < 1700
		if (annotationVariable->AsScalar()->GetBool(&value)==S_OK) {
			return true;
		}
#else
		if (annotationVariable->AsScalar()->GetBool((bool*)&value)==S_OK) {
			return true;
		}
#endif
	}
	return false;
}

void CUniformParameterBuilder::logUnsupportedTypeWarning(const MStringResourceId& typeId)
{
	MString typeStr = dx11ShaderStrings::getString( typeId );

	MStringArray args;
	args.append(typeStr);
	args.append(mDesc.Name);

	mWarnings += dx11ShaderStrings::getString( dx11ShaderStrings::kUnsupportedType, args );
}
void CUniformParameterBuilder::logUnrecognisedSemantic(const char* pSemantic)
{
	MStringArray args;
	args.append(pSemantic);
	args.append(mDesc.Name);

	mWarnings += dx11ShaderStrings::getString( dx11ShaderStrings::kUnknowSemantic, args );
}
