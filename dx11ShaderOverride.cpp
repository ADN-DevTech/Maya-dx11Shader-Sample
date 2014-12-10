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

#include <dx11ShaderOverride.h>
#include <maya/MString.h>
#include <maya/MDrawContext.h>

static const bool DO_CUSTOM_DRAW = true;
static const bool PRINT_DEBUG_INFO = false;

MHWRender::MPxShaderOverride* dx11ShaderOverride::Creator(const MObject& obj)
{
	return new dx11ShaderOverride(obj);
}

dx11ShaderOverride::dx11ShaderOverride(const MObject& obj)
: MHWRender::MPxShaderOverride(obj)
, fShaderNode(NULL)
, fGeometryVersionId(0)
, fBBoxExtraScale(1.0f)
, fStates()
{
	// Get an early peek to the shader node, so we can have the scale value,
	// before the shader can be discarded by the clipping.
	dx11ShaderNode* shaderNode = (dx11ShaderNode*)MPxHardwareShader::getHardwareShaderPtr(const_cast<MObject&>(obj));
	if(shaderNode)
	{
		fBBoxExtraScale = shaderNode->boundingBoxExtraScale();
	}
}

dx11ShaderOverride::~dx11ShaderOverride()
{
}

MString dx11ShaderOverride::initialize(const MInitContext& initContext,MInitFeedback& initFeedback)
{
	// Get the hardware shader node from the MObject
	// -----------------------------------------------
	fShaderNode = NULL;
	if (initContext.shader != MObject::kNullObj) {
		MInitContext* nonConst = const_cast<MInitContext*>(&initContext);
		fShaderNode =(dx11ShaderNode*)MPxHardwareShader::getHardwareShaderPtr(nonConst->shader);
	}

	// Add geometry requirements based on
	// -----------------------------------------------
	if (fShaderNode)
	{
		int count			= fShaderNode->techniqueCount();
		int activeTechnique = fShaderNode->activeTechnique();
		if (activeTechnique >= 0 && activeTechnique < count)
		{
			// add requirements
			addGeometryRequirements(*fShaderNode->vertexBufferDescLists());

			// Trying to send the signature anyhow in order not to have the assert
			// (maybe not necessary because for custom draw we create the input
			// layout manually)
			dx11ShaderDX11EffectTechnique* technique = fShaderNode->technique();
			if (technique)
			{
				int passCount = fShaderNode->passCount();
				if (passCount > 0)
				{
					// Take signature from first pass (assume all equal)
					dx11ShaderDX11Pass* dxPass = technique->GetPassByIndex(0);
					if (dxPass)
					{
						D3DX11_PASS_DESC descPass;
						dxPass->GetDesc(&descPass);
						addShaderSignature( descPass.pIAInputSignature, descPass.IAInputSignatureSize);
					}
				}
			}
		}

		// custom primitive types can be used by shader overrides.
		// This code is a simple example to show the mechanics of how that works.
		// Here we declare a custom custom indexing requirements.
		// The customPrimitiveGeneratorName will be used to look up a registered
		// MPxPrimitiveGenerator that will handle the generation of the index buffer.
		// The example primitive generator is registered at startup by this plugin.
		MString customPrimitiveGeneratorName = fShaderNode->techniqueIndexBufferType();
		// Always set the custom indexing requirement, even it the name is empty;
		// That will clear any custom name that was previously set (may happen during reload).
		//if (customPrimitiveGeneratorName.length() > 0)
		{
			MHWRender::MIndexBufferDescriptor indexingRequirement
				(MHWRender::MIndexBufferDescriptor::kCustom, customPrimitiveGeneratorName, MHWRender::MGeometry::kTriangles);
			addIndexingRequirement(indexingRequirement);
		}

		// Sync version ids
		fGeometryVersionId = fShaderNode->geometryVersionId();
	}

	// Build key string, note that if any attribute on the node changes that
	// would affect the value of this string, then we must trigger rebuild of
	// the shader
	MString result = MString("Autodesk Maya dx11ShaderOverride, nodeName=");
	result += fShaderNode ? fShaderNode->name() : MString("null");
	result += MString(", effectFileName=");
	result += fShaderNode ? fShaderNode->effectName() : MString("null");
	result += MString(", technique=");
	result += (fShaderNode) ? fShaderNode->activeTechniqueName() : MString("null");
	return result;
}

void dx11ShaderOverride::updateDG(MObject object)
{
}

void dx11ShaderOverride::updateDevice()
{
}

void dx11ShaderOverride::endUpdate()
{
}

bool dx11ShaderOverride::handlesDraw(MHWRender::MDrawContext& context)
{
	const MHWRender::MPassContext & passCtx = context.getPassContext();
	const MStringArray & passSem = passCtx.passSemantics();

	// For color passes, only handle if there isn't already
	// a global override. This is the same as the default
	// logic for this method in MPxShaderOverride
	//
	bool handlePass = false;
	for (unsigned int i=0; i<passSem.length() && !handlePass; i++)
	{
		if (passSem[i] == MHWRender::MPassContext::kColorPassSemantic)
		{
			bool hasOverrideShader = passCtx.hasShaderOverride();
			if (!hasOverrideShader)
				handlePass = true;
		}

		// Handle special pass drawing.
		//
		else if (passSem[i] == MHWRender::MPassContext::kShadowPassSemantic ||
				 passSem[i] == MHWRender::MPassContext::kDepthPassSemantic ||
				 passSem[i] == MHWRender::MPassContext::kNormalDepthPassSemantic ||
				 passSem[i] == MHWRender::MPassContext::kTransparentPeelSemantic ||
				 passSem[i] == MHWRender::MPassContext::kTransparentPeelAndAvgSemantic ||
				 passSem[i] == MHWRender::MPassContext::kTransparentWeightedAvgSemantic)
		{
			handlePass = fShaderNode->techniqueHandlesContext(passSem[i]);
		}
	}
	return handlePass;
}

void dx11ShaderOverride::activateKey(MHWRender::MDrawContext& context, const MString& /*key*/)
{
	if (fShaderNode) {
		MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
		if (theRenderer)
		{
			dx11ShaderDX11Device* dxDevice = (dx11ShaderDX11Device*)theRenderer->GPUDeviceHandle();
			if (dxDevice)
			{
				ID3D11DeviceContext* dxContext = NULL;
				dxDevice->GetImmediateContext(&dxContext);
				if (dxContext)
				{
					fShaderNode->backupStates(dxContext, fStates);

					dxContext->VSSetShader(NULL, NULL, 0);
					dxContext->PSSetShader(NULL, NULL, 0);
					dxContext->GSSetShader(NULL, NULL, 0);
					dxContext->HSSetShader(NULL, NULL, 0);
					dxContext->DSSetShader(NULL, NULL, 0);

					dxContext->Release();
				}
			}
		}

		fShaderNode->updateShaderBasedGeoChanges();
	}
}

bool dx11ShaderOverride::draw(
	MHWRender::MDrawContext& context,
	const MHWRender::MRenderItemList& renderItemList) const
{
	// Get effect wrapper
	if (!fShaderNode) return false;

	// Sample code to debug pass information
	static const bool debugPassInformation = false;
	if (debugPassInformation)
	{
		const MHWRender::MPassContext & passCtx = context.getPassContext();
		const MString & passId = passCtx.passIdentifier();
		const MStringArray & passSem = passCtx.passSemantics();
		printf("dx11 shader drawing in pass[%s], semantic[", passId.asChar());
		for (unsigned int i=0; i<passSem.length(); i++)
			printf(" %s", passSem[i].asChar());
		printf("\n");
	}

	return fShaderNode->render(context, renderItemList);
}

void dx11ShaderOverride::terminateKey(MHWRender::MDrawContext& context, const MString& /*key*/)
{
	if (fShaderNode) {
		MHWRender::MRenderer* theRenderer = MHWRender::MRenderer::theRenderer();
		if (theRenderer)
		{
			dx11ShaderDX11Device* dxDevice = (dx11ShaderDX11Device*)theRenderer->GPUDeviceHandle();
			if (dxDevice)
			{
				ID3D11DeviceContext* dxContext = NULL;
				dxDevice->GetImmediateContext(&dxContext);
				if (dxContext)
				{
					fShaderNode->restoreStates(dxContext, fStates);

					dxContext->VSSetShader(NULL, NULL, 0);
					dxContext->PSSetShader(NULL, NULL, 0);
					dxContext->GSSetShader(NULL, NULL, 0);
					dxContext->HSSetShader(NULL, NULL, 0);
					dxContext->DSSetShader(NULL, NULL, 0);

					dxContext->Release();
				}
			}
		}
	}
}

MHWRender::DrawAPI dx11ShaderOverride::supportedDrawAPIs() const
{
	return MHWRender::kDirectX11;
}

bool dx11ShaderOverride::isTransparent()
{
	return (fShaderNode && fShaderNode->techniqueIsTransparent());
}

bool dx11ShaderOverride::supportsAdvancedTransparency() const
{
	return (fShaderNode && fShaderNode->techniqueSupportsAdvancedTransparency());
}

bool dx11ShaderOverride::overridesDrawState()
{
	return (fShaderNode && fShaderNode->techniqueOverridesDrawState());
}

bool dx11ShaderOverride::rebuildAlways()
{
	return !fShaderNode || fShaderNode->rebuildAlways(fGeometryVersionId);
}

double dx11ShaderOverride::boundingBoxExtraScale() const
{
	if(fShaderNode != NULL)
		fBBoxExtraScale = fShaderNode->boundingBoxExtraScale();

	return fBBoxExtraScale;
}
