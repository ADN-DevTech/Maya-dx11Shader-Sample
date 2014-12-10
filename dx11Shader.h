#ifndef _dx11ShaderNode_h_
#define _dx11ShaderNode_h_
//-
// Copyright 2011 Autodesk, Inc.  All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk license agreement
// provided at the time of installation or download, or which otherwise
// accompanies this software in either electronic or hard copy form.
//+

#include <maya/MPxHardwareShader.h>
#include <maya/MStringArray.h>
#include <maya/MVaryingParameterList.h>
#include <maya/MUniformParameterList.h>
#include <maya/MHWGeometry.h>
#include <maya/MPlugArray.h>

#include <maya/MMessage.h>

// Includes for DX11
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>

// for VS 2012, Win8 SDK includes DX sdk with some header removed
#if _MSC_VER >= 1700
#include <dxgi.h>
#else
#include <d3dx11.h>
#endif

#define HAVE_D3DX11EFFECT_LIBRARY_BUILT
#if defined(HAVE_D3DX11EFFECT_LIBRARY_BUILT)
	// To build against the DX SDK header use the following commented line
	//#include <../Samples/C++/Effects11/Inc/d3dx11effect.h>
	#include <maya/d3dx11effect.h>
	#define dx11ShaderDX11Device ID3D11Device
	#define dx11ShaderDX11DeviceContext ID3D11DeviceContext
	#define dx11ShaderDX11Effect ID3DX11Effect
	#define dx11ShaderDX11EffectTechnique ID3DX11EffectTechnique
	#define dx11ShaderDX11Pass ID3DX11EffectPass
	#define dx11ShaderDX11InputLayout ID3D11InputLayout
	#define dx11ShaderDX11InputElementDesc D3D11_INPUT_ELEMENT_DESC
	#define dx11ShaderDX11EffectVariable ID3DX11EffectVariable
	#define dx11ShaderDX11EffectShaderResourceVariable ID3DX11EffectShaderResourceVariable
	#define dx11ShaderDX11RasterizerState ID3D11RasterizerState
	#define dx11ShaderDX11DepthStencilState ID3D11DepthStencilState
	#define dx11ShaderDX11BlendState ID3D11BlendState
#else
	#define dx11ShaderDX11Device void
	#define dx11ShaderDX11DeviceContext void
	#define dx11ShaderDX11Effect void
	#define dx11ShaderDX11EffectTechnique void
	#define dx11ShaderDX11Pass void
	#define dx11ShaderDX11InputLayout void
	#define dx11ShaderDX11InputElementDesc void
	#define dx11ShaderDX11EffectVariable void
	#define dx11ShaderDX11EffectShaderResourceVariable void
	#define dx11ShaderDX11RasterizerState void
	#define dx11ShaderDX11DepthStencilState void
	#define dx11ShaderDX11BlendState void
#endif
#include <map>
#include <set>
#include <vector>

class CUniformParameterBuilder;
class MRenderProfile;

namespace MHWRender {
	class MLightParameterInformation;
	class MTexture;
	class MRenderTarget;
	class MDrawContext;
}

#define USE_GL_TEXTURE_CACHING

////////////////////////////////////////////////////////
//
// Maya hardware shader node
//
class dx11ShaderNode : public MPxHardwareShader
{
public:
	// Identify the purpose of the current rendering process
	enum ERenderType
	{
		RENDER_SCENE,				// Render the scene to the viewport 2.0
		RENDER_SWATCH,				// Render the swatch that represents the current selected technique
		RENDER_SWATCH_PROXY,		// Render a dummy swatch when no effect or no valid technique selected
		RENDER_UVTEXTURE,			// Render a texture for the UV editor
		RENDER_SCENE_DEFAULT_LIGHT	// Render the scene using a default light
	};

	enum ELightType
	{
		eInvalidLight,
		eUndefinedLight,
		eSpotLight,
		ePointLight,
		eDirectionalLight,
		eAmbientLight,
		eVolumeLight,
		eAreaLight,
		eDefaultLight,

		eLightCount
	};

	// Identify the transparency state of the selected technique
	enum ETransparencyState
	{
		eOpaque,				// Technique is always opaque
		eTransparent,			// Technique is always transparent
		eTestOpacitySemantics,	// Technique transparency depends on the value of the float parameter with kOpacity semantic ( transparent if less than 1.0)
		eScriptedTest			// Technique transparency depends on the result of the transparencyTest MEL procedure
	};

	struct ContextStates
	{
		ContextStates() : rasterizerState(NULL), depthStencilState(NULL), blendState(NULL) {}

		dx11ShaderDX11RasterizerState*		rasterizerState;
		dx11ShaderDX11DepthStencilState*	depthStencilState;
		UINT								stencilRef;
		dx11ShaderDX11BlendState*			blendState;
		float								blendFactor[4];
		UINT								sampleMask;
	};

	class LightParameterInfo
	{
		typedef dx11ShaderNode::ELightType ELightType;

	public:
		LightParameterInfo(ELightType lightType = dx11ShaderNode::eInvalidLight, bool hasLightTypeSemantics = false);

		ELightType	lightType() const;

	public:
		ELightType	fLightType;
		bool		fHasLightTypeSemantics;
		bool		fIsDirty;

		// This is a map<MUniformParameterList->index, ELightParameterType>
		typedef std::map<int, int> TConnectableParameters;
		TConnectableParameters fConnectableParameters;

		MObject		fAttrUseImplicit;
		MObject		fAttrConnectedLight;
		MObject		fCachedImplicitLight;
	};

public:
	// Constructor/Destructor housekeeping: create, copy setup
						dx11ShaderNode();
	virtual				~dx11ShaderNode();

	static  MTypeId		typeId();
	static  void*		creator();
	static  MStatus		initialize();
	static  void        initializeNodeAttrs();

	// Query the renderers supported by this shader
	//
	virtual const MRenderProfile& profile();

public:
	// Internal attribute housekeeping
	virtual void		copyInternalData( MPxNode* pSrc );
	static	void		postDuplicateCB( void *data );
	virtual bool		getInternalValueInContext( const MPlug&,MDataHandle&,MDGContext&);
    virtual bool		setInternalValueInContext( const MPlug&,const MDataHandle&,MDGContext&);

	virtual MStatus		connectionMade( const MPlug& plug, const MPlug& otherPlug, bool asSrc );

	// Dynamic light connection housekeeping:
	virtual MStatus		setDependentsDirty(const MPlug & plugBeingDirtied, MPlugArray & affectedPlugs);

	/////////////////////////////////
	// Topology Management
public:
	bool rebuildAlways(size_t baseVersionId) const;
	bool isDirty(size_t baseVersionId) const;
	size_t geometryVersionId() const;

private:
	bool hasUpdatedVaryingInput() const;
	void setTopoDirty();

	/////////////////////////////////
	// Effect Management
public:
	static bool reloadAll(const MString& effectName);
	bool reload();

	const MString& effectName() const;
	dx11ShaderDX11Effect* effect() const;

	double boundingBoxExtraScale() const;

private:
	bool loadEffect( const MString& effectName );

	bool loadFromFile( const MString& fileName, dx11ShaderDX11Device* dxDevice);
	bool loadFromBuffer( const MString& identifier, const void* pData, unsigned int dataSize, dx11ShaderDX11Device* dxDevice);

	bool initializeEffect();
	void resetData(bool clearEffect = true);

	/////////////////////////////////
	// Technique Management
public:
	const MStringArray& techniques() const;
	inline int techniqueCount() const;

	bool techniqueIsTransparent() const;
	bool techniqueSupportsAdvancedTransparency() const;
	bool techniqueOverridesDrawState() const;

	// Does the technique know how to render shadows or other special context?
	bool techniqueHandlesContext(const MString& requestedContext) const;

	// Return the active technique number. Will be -1 if none
	int activeTechnique() const;

	// Return pointer to active technique
	dx11ShaderDX11EffectTechnique* technique() const;

	// Return name of active technique
	const MString& activeTechniqueName() const;

	// Return name of index buffer type of active technique
	const MString& techniqueIndexBufferType() const;

private:
	bool initializeTechniques();
	bool setTechnique( const MString& techniqueName );
	bool setTechnique( int techniqueNumber );

	void initTechniqueParameters();

	void storeDefaultTextureNames();
	void restoreDefaultTextureNames();

	/////////////////////////////////
	// Pass Management
public:
	// Return the number of pass in active technique
	int passCount() const;

private:
	dx11ShaderDX11Pass* activatePass( dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11EffectTechnique* dxTechnique, unsigned int passId, ERenderType renderType ) const;
	dx11ShaderDX11Pass* activatePass( dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11EffectTechnique* dxTechnique, unsigned int passId, const MStringArray& passSem, ERenderType renderType ) const;

	bool passHasHullShader(dx11ShaderDX11Pass* dxPass) const;
	dx11ShaderDX11InputLayout* getInputLayout(dx11ShaderDX11Device* dxDevice, dx11ShaderDX11Pass* dxPass, unsigned int numLayouts, const dx11ShaderDX11InputElementDesc* layoutDesc) const;

	/////////////////////////////////
	// Rendering
public:
	// Render to GL viewport
	virtual MStatus render( MGeometryList& iterator);

	// Override this method to draw a image for swatch rendering.
	///
	virtual MStatus renderSwatchImage( MImage & image );

	// Override these methods to support texture display in the UV texture editor.
	//
	virtual MStatus getAvailableImages( const MPxHardwareShader::ShaderContext &context, const MString& uvSetName, MStringArray &imageNames );
	virtual MStatus renderImage( const MPxHardwareShader::ShaderContext &context, const MString& imageName, floatRegion region, const MPxHardwareShader::RenderParameters& parameters, int &imageWidth, int &imageHeight );
	virtual MStatus renderImage( const MPxHardwareShader::ShaderContext &context, MHWRender::MUIDrawManager& uiDrawManager, const MString& imageName, floatRegion region, const MPxHardwareShader::RenderParameters& parameters, int &imageWidth, int &imageHeight );

	// Render to DX vp2
	bool render(const MHWRender::MDrawContext& context, const MHWRender::MRenderItemList& renderItemList);

private:
	typedef std::vector<const MHWRender::MRenderItem*> RenderItemList;

	// Render functions for a list of render items
	bool renderTechnique(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11EffectTechnique* dxTechnique,
					unsigned int numPasses, const MStringArray& passSem,
					const RenderItemList& renderItemList,
					const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const;
	bool renderPass(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11Pass* dxPass,
					const RenderItemList& renderItemList,
					const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const;

	// Render functions for a single geometry
	bool renderTechnique(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11EffectTechnique* dxTechnique, unsigned int numPasses,
					const MHWRender::MGeometry* geometry, MHWRender::MGeometry::Primitive primitiveType, unsigned int primitiveStride,
					const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType ) const;

	bool renderPass(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11DeviceContext *dxContext, dx11ShaderDX11Pass* dxPass,
					const MHWRender::MGeometry* geometry, MHWRender::MGeometry::Primitive primitiveType, unsigned int primitiveStride,
					const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const;

	// Render function for a single geometry into a texture target
	bool renderTechnique(dx11ShaderDX11Device *dxDevice, dx11ShaderDX11EffectTechnique* dxTechnique, unsigned int numPasses,
					MHWRender::MRenderTarget* textureTarget, unsigned int width, unsigned int height, float clearColor[4],
					const MHWRender::MGeometry* geometry, MHWRender::MGeometry::Primitive primitiveType, unsigned int primitiveStride,
					const MVaryingParameterList& varyingParameters, ERenderType renderType, const MString& indexBufferType) const;

public:
	void backupStates(dx11ShaderDX11DeviceContext *dxContext, ContextStates &states) const;
	void restoreStates(dx11ShaderDX11DeviceContext *dxContext, ContextStates &states) const;

private:
	typedef std::map< dx11ShaderDX11EffectShaderResourceVariable*, MHWRender::MTexture* > ResourceTextureMap;
	bool updateParameters( const MHWRender::MDrawContext& context, MUniformParameterList& uniformParameters, ResourceTextureMap &resourceTexture, ERenderType renderType ) const;
	void updateViewportGlobalParameters( const MHWRender::MDrawContext& context ) const;

public:
	void updateShaderBasedGeoChanges();

private:
	typedef std::map<int, bool> TshadowFlagBackupState;
	void initShadowFlagBackupState(TshadowFlagBackupState& stateBackup ) const;
	void setPerGeometryShadowOnFlag(bool receivesShadows, TshadowFlagBackupState& stateBackup ) const;

	/////////////////////////////////
	// Uniform and varying parameters
public:
	void clearParameters();

private:
	// Uniform and varying parameters
	void preBuildUniformParameterList();
	bool buildUniformParameterList();
	bool buildVaryingParameterList();

	bool buildVertexDescriptorFromVaryingParameters();

	// Internal
	void initMayaParameters();

	/////////////////////////////////
	// Attibute Editor
public:
	const MStringArray& getUIGroups() const;
	MStringArray getUIGroupParameters(int uiGroupIndex) const;
	int getIndexForUIGroupName(const MString& uiGroupName, bool appendGroup = false);

	const MStringArray& lightInfoDescription() const;

	MString getLightConnectionInfo(int lightIndex);
	MStringArray getLightableParameters(int lightIndex, bool showSemantics);
	int getIndexForLightName(const MString& lightName, bool appendLight = false);

	bool getVariableNameAsAttributeName(){ return fVariableNameAsAttributeName; }

private:
	bool appendParameterNameIfVisible(int paramIndex, MStringArray& paramArray) const;

	/////////////////////////////////
	// Light Management
public:
	void refreshLightConnectionAttributes(bool inSceneUpdateNotification=false);

	void connectLight(int lightIndex, MDagPath light);
	void disconnectLight(int lightIndex);

private:
	void refreshView() const;
	void setLightRequiresShadows(const MObject& lightObject, bool requiresShadow) const;
	void updateImplicitLightConnections(const MHWRender::MDrawContext& context, ERenderType renderType) const;
	void updateExplicitLightConnections(const MHWRender::MDrawContext& context, ERenderType renderType) const;

private:
	void updateImplicitLightParameterCache(std::vector<CUniformParameterBuilder*>& builders);
	void clearLightConnectionData();

private:
	void getLightParametersToUpdate(std::set<int>& parametersToUpdate, ERenderType renderType) const;

	void connectLight(const LightParameterInfo& lightInfo, MHWRender::MLightParameterInformation* lightParam, ERenderType renderType=RENDER_SCENE) const;
	bool connectExplicitAmbientLight(const LightParameterInfo& lightInfo, const MObject& sourceLight) const;
	void turnOffLight(const LightParameterInfo& lightInfo) const;
	void setLightParameterLocking(const LightParameterInfo& lightInfo, bool locked) const;

	/////////////////////////////////
	// Texture Management
private:
	MHWRender::MTexture* loadTexture(const MString& textureName, const MString& layerName, int alphaChannelIdx, int mipmapLevels) const;
	void releaseTexture(MHWRender::MTexture* texture) const;
	void assignTexture(dx11ShaderDX11EffectShaderResourceVariable* resourceVariable, const MString& textureName, const MString& layerName, int alphaChannelIdx, ResourceTextureMap& resourceTexture) const;
	void releaseAllTextures(ResourceTextureMap& resourceTexture) const;
	void releaseAllTextures();
  
  MHWRender::MTexture* getUVTexture(MHWRender::MDrawContext *context, const MString& imageName, int& imageWidth, int& imageHeight);
  MHWRender::MTexture* getUVTexture(MHWRender::MDrawContext *context, const MString& imageName, int& imageWidth, int& imageHeight,
      MString &textureName, MString& layerName, int &alphaChannelIdx, int &mipmapLevels);

public:
	bool getTextureFile(const MString& uniformName, MString& textureFile) const;

	/////////////////////////////////
	// Convenient functions
private:
	void setParameterAsVector(int paramIndex, float* data) const;
	void setParameterAsScalar(int paramIndex, float data) const;
	void setParameterAsScalar(int paramIndex, bool data) const;
	void setParameterAsScalar(int paramIndex, int data) const;
	void setParameterAsMatrix(int paramIndex, MMatrix& data) const;
	void setParameterAsResource(int paramIndex, ID3D11ShaderResourceView* inResource) const;
	void setParameterFromUniformAsVector(int paramIndex,const MHWRender::MDrawContext& context, const float *data = NULL) const;
	void setParameterFromUniformAsScalar(int paramIndex,const MHWRender::MDrawContext& context) const;

	/////////////////////////////////
public:
	const MHWRender::MVertexBufferDescriptorList* vertexBufferDescLists();

	/////////////////////////////////
	// Diagnostics/description strings
private:
	void displayErrorAndWarnings() const;
	void reportInternalError( const char* function, size_t errcode ) const;

	/////////////////////////////////
	// External content management	
private:
#if defined(MAYA_WANT_EXTERNALCONTENTTABLE)
	virtual	void getExternalContent(MExternalContentInfoTable& table) const;
	virtual	void setExternalContent(const MExternalContentLocationTable& table);
#endif

private:
	// Version id, used by VP2.0 override to determine when a rebuild is necessary
	size_t							fGeometryVersionId;

	// Keeps track if the anything in the shader may change the geo
	bool							fShaderChangesGeo;
	double							fLastTime;

	// Force the shader variable name to become the Maya attribute name, regardless of UIName annotation
	bool							fVariableNameAsAttributeName;

	// Identifier to track scene-render-frame in order to optimize the updateParameter routine.
	mutable MUint64					fLastFrameStamp;

	// For duplicate
	dx11ShaderNode*					fDuplicateNodeSource;
	MCallbackId						fPostDuplicateCallBackId;
	MPlugArray						fDuplicatedConnections;

	///////////// Effect Management
	// Effect name
	MString							fEffectName;
	// Pointer to effect
	dx11ShaderDX11Effect*			fEffect;

	///////////// Technique Management
	// List of techniques by name
	MStringArray					fTechniqueNames;

	// Active technique index
	int								fTechniqueIdx;
	// Active technique name
	MString							fTechniqueName;
	// Pointer to active technique
	dx11ShaderDX11EffectTechnique*	fTechnique;
	// Active technique mipmapLevels value when loading textures
	int								fTechniqueTextureMipMapLevels;
	// Active technique custom primitive generator that will be used to generate the index buffer.
	MString							fTechniqueIndexBufferType;

	ETransparencyState				fTechniqueIsTransparent;
	MString							fOpacityPlugName;
	MString							fTransparencyTestProcName;
	bool							fTechniqueSupportsAdvancedTransparency;
	bool							fTechniqueOverridesDrawState;

	// The enum version of .technique attribute (node local dynamic attr)
	MObject							fTechniqueEnumAttr;

	///////////// Pass Management
	// Active technique pass count
	unsigned int					fPassCount;

	///////////// Uniform Parameters
	// List of uuniform parameters.
	MUniformParameterList			fUniformParameters;

	///////////// Varying Parameters
	// List of vertex buffer descriptions.
	MVaryingParameterList					fVaryingParameters;
	MHWRender::MVertexBufferDescriptorList	fVaryingParametersVertexDescriptorList;
	size_t									fVaryingParametersGeometryVersionId;
	size_t									fVaryingParametersUpdateId;

	// default UV editor textures
	MStringArray							fDefaultTextureNames;

	///////////// Light
	typedef std::vector<LightParameterInfo> LightParameterInfoVec;
	LightParameterInfoVec			fLightParameters;
	MStringArray					fLightNames;
	MStringArray					fLightDescriptions;
	mutable int						fImplicitAmbientLight;

	///////////// Attibute Editor
	MStringArray					fUIGroupNames;
	std::vector<std::vector<int> >	fUIGroupParameters;

	///////////// Texture Management
	ResourceTextureMap				fResourceTextureMap;
	mutable bool					fForceUpdateTexture;
	int								fFixedTextureMipMapLevels;
	MHWRender::MTexture*			fUVEditorTexture;

#ifdef USE_GL_TEXTURE_CACHING
	// Caching for UV Texture image
	MString							fUVEditorLastTexture;
	MString                         fUVEditorLastLayer;
    int                             fUVEditorLastAlphaChannel;
	float							fUVEditorBaseColor[4];
	bool							fUVEditorShowAlphaMask;
	unsigned int					fUVEditorGLTextureId;
	float							fUVEditorGLTextureScaleU;
	float							fUVEditorGLTextureScaleV;
#endif //USE_GL_TEXTURE_CACHING

	// Bounding Box Extra Scale
	MString							fBBoxExtraScalePlugName;
	double							fBBoxExtraScaleValue;

	// Maya Swatch Render
	dx11ShaderDX11EffectVariable*	fMayaSwatchRenderVar;

	// Maya full screen gamma correction
	dx11ShaderDX11EffectVariable*	fMayaGammaCorrectVar;

	///////////// Some caching
	typedef std::map< dx11ShaderDX11Pass*, bool > PassHasHullShaderMap;
	mutable PassHasHullShaderMap	fPassHasHullShaderMap;

	struct CachedInputElementDesc
	{
		MString	SemanticName;
		unsigned int SemanticIndex;
		int Format;
		unsigned int InputSlot;
		unsigned int AlignedByteOffset;
		int InputSlotClass;
		unsigned int InstanceDataStepRate;
	};

	struct InputLayoutData
	{
		dx11ShaderDX11InputLayout* inputLayout;
		unsigned int numLayouts;
		CachedInputElementDesc* layoutDesc;
	};
	typedef std::map< dx11ShaderDX11Pass*, InputLayoutData > PassInputLayoutMap;
	mutable PassInputLayoutMap		fPassInputLayoutMap;

	///////////// Diagnostics/description strings
	mutable MString					fErrorLog;
	mutable MString					fWarningLog;
	mutable unsigned int			fErrorCount;
};


// INLINE

/////////////////////////////////
// Topology Management
inline bool dx11ShaderNode::rebuildAlways(size_t baseVersionId) const
{
	return hasUpdatedVaryingInput() || isDirty(baseVersionId);
}

inline bool dx11ShaderNode::isDirty(size_t baseVersionId) const
{
	return (fGeometryVersionId != baseVersionId);
}

inline size_t dx11ShaderNode::geometryVersionId() const
{
	return fGeometryVersionId;
}

/////////////////////////////////
// Effect Management
inline const MString& dx11ShaderNode::effectName() const
{
	return fEffectName;
}

inline dx11ShaderDX11Effect* dx11ShaderNode::effect() const
{
	return fEffect;
}

inline double dx11ShaderNode::boundingBoxExtraScale() const
{
	return (fBBoxExtraScaleValue > 1.0f ? fBBoxExtraScaleValue : 1.0f);
}

/////////////////////////////////
// Technique Management
inline const MStringArray& dx11ShaderNode::techniques() const
{
	return fTechniqueNames;
}

inline int dx11ShaderNode::techniqueCount() const
{
	return (int)fTechniqueNames.length();
}

inline int dx11ShaderNode::activeTechnique() const
{
	return fTechniqueIdx;
}

inline dx11ShaderDX11EffectTechnique* dx11ShaderNode::technique() const
{
	return fTechnique;
}

inline const MString& dx11ShaderNode::activeTechniqueName() const
{
	return fTechniqueName;
}

inline const MString& dx11ShaderNode::techniqueIndexBufferType() const
{
	return fTechniqueIndexBufferType;
}

inline bool dx11ShaderNode::techniqueOverridesDrawState() const
{
	return fTechniqueOverridesDrawState;
}

/////////////////////////////////
// Pass Management
inline int dx11ShaderNode::passCount() const
{
	return fPassCount;
}

/////////////////////////////////
// Attibute Editor
inline const MStringArray& dx11ShaderNode::getUIGroups() const
{
	return fUIGroupNames;
}

inline const MStringArray& dx11ShaderNode::lightInfoDescription() const
{
	return fLightDescriptions;
}

#endif /* _dx11ShaderNode_h_ */

