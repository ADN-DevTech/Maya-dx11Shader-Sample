#ifndef _dx11ShaderUniformParamBuilder_h_
#define _dx11ShaderUniformParamBuilder_h_
//-
// ==========================================================================
// Copyright 1995,2006,2008,2011 Autodesk, Inc. All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk
// license agreement provided at the time of installation or download,
// or which otherwise accompanies this software in either electronic
// or hard copy form.
// ==========================================================================
//+
#include <maya/MUniformParameter.h>

// Includes for DX11
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>

// for VS 2012, Win8 SDK includes DX sdk with some header removed
#if _MSC_VER >= 1700
#include <dxgi.h>
#else
#include <d3dx11.h>
#endif

// To build against the DX SDK header use the following commented line
//#include <../Samples/C++/Effects11/Inc/d3dx11effect.h>
#include <maya/d3dx11effect.h>
#include <map>
#include <string>


class dx11ShaderNode;
class MStringResourceId;

class CUniformParameterBuilder
{
public:
	CUniformParameterBuilder();
	

	void init(ID3DX11EffectVariable* inEffectVariable,dx11ShaderNode* inShader, int order);

	bool build();

	bool isValidUniformParameter()
	{return mValidUniformParameter;}

	MUniformParameter& getParameter()
	{return mParam;}

	ID3DX11EffectVariable* getEffectVariable()
	{return mEffectVariable; }

	MString& getWarnings()
	{return mWarnings;}


	enum ELightParameterType
	{
		eUndefined, // 0
		eLightPosition,
		eLightDirection,
		eLightColor,
		eLightSpecularColor,
		eLightAmbientColor, // 5
		eLightDiffuseColor,
		eLightRange,
		eLightFalloff,
		eLightAttenuation0,
		eLightAttenuation1, // 10
		eLightAttenuation2,
		eLightTheta,
		eLightPhi,
		eLightShadowMap,
		eLightShadowMapBias, // 15
		eLightShadowColor,
		eLightShadowViewProj,
		eLightShadowOn,
		eLightIntensity,
		eLightHotspot, // 20
		eLightEnable,
		eLightType,
		eDecayRate,
		eLightAreaPosition0,
		eLightAreaPosition1, // 25
		eLightAreaPosition2,
		eLightAreaPosition3,

		// When updating this array, please keep the 
		// strings in getLightParameterSemantic in sync. 
		//    Thanks!
		eLastParameterType,
	};
	ELightParameterType  getLightParameterType()
	{return mParamType;}

	// Get semantic string back from enum:
	static MString& getLightParameterSemantic(int lightParameterType);

	enum ELightType
	{
		eNotLight,
		eUndefinedLight,
		ePointLight,
		eSpotLight,
		eDirectionalLight,
		eAmbientLight,
		eAreaLight,
	};

	ELightType getLightType()
	{return mLightType;}

	int getLightIndex()
	{ return mLightIndex; }

	int getUIGroupIndex()
	{ return mUIGroupIndex; }

	int getUIOrder()
	{ return mUIOrder;	}

	static bool compareUIOrder(CUniformParameterBuilder* a, CUniformParameterBuilder* b)
	{return (a->getUIOrder() < b->getUIOrder());}

protected:


	bool getAnnotation(const char* name,LPCSTR& value);
	bool getAnnotation(const char* name,float& value);
	bool getAnnotation(const char* name,int& value);
	bool getBOOLAnnotation(const char* name,BOOL& value);
	ID3DX11EffectVariable* findAnnotationByName(ID3DX11EffectVariable *effectVariable, const char* name);

	int getLength()
	{
		int								rows		= mDescType.Rows;
		int								columns		= mDescType.Columns;
		return rows * columns;
	}

	bool setParameterValueFromEffect();
	MUniformParameter::DataType convertType();
	MUniformParameter::DataSemantic convertSpace( MUniformParameter::DataSemantic defaultSpace);
	void updateRangeFromAnnotation();
	void updateUIVisibilityFromAnnotation();
	MUniformParameter::DataSemantic convertSemantic();
	void updateLightInfoFromSemantic();


	void logUnsupportedTypeWarning(const MStringResourceId& typeId);
	void logUnrecognisedSemantic(const char* pSemantic);


	ID3DX11EffectVariable*		mEffectVariable;
	dx11ShaderNode *			mShader;
	D3DX11_EFFECT_VARIABLE_DESC mDesc;
	D3DX11_EFFECT_TYPE_DESC		mDescType;
	MUniformParameter			mParam;
	ELightParameterType			mParamType;
	MString						mWarnings;
	ELightType					mLightType;
	int							mLightIndex;
	int							mUIGroupIndex;
	bool						mValidUniformParameter;
	int							mUIOrder;
	typedef std::map<std::string,uint32_t> TAnnotationIndex;
	TAnnotationIndex			mAnnotationIndex;
};


#endif /* _dx11ShaderUniformParamBuilder_h_ */
