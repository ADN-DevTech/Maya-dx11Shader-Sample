#ifndef _dx11ShaderOverride_h_
#define _dx11ShaderOverride_h_
//-
// Copyright 2011 Autodesk, Inc.  All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk license agreement
// provided at the time of installation or download, or which otherwise
// accompanies this software in either electronic or hard copy form.
//+

#include <maya/MPxShaderOverride.h>
#include "dx11Shader.h"


class dx11ShaderOverride : public MHWRender::MPxShaderOverride
{
public:
	static MHWRender::MPxShaderOverride* Creator(const MObject& obj);

	virtual ~dx11ShaderOverride();

	virtual MString initialize( const MInitContext& initContext,MInitFeedback& initFeedback);

	virtual void updateDG(MObject object);
	virtual void updateDevice();
	virtual void endUpdate();

	virtual bool handlesDraw(MHWRender::MDrawContext& context);
	virtual void activateKey(MHWRender::MDrawContext& context, const MString& key);
	virtual bool draw(MHWRender::MDrawContext& context,const MHWRender::MRenderItemList& renderItemList) const;
	virtual void terminateKey(MHWRender::MDrawContext& context, const MString& key);

	virtual MHWRender::DrawAPI supportedDrawAPIs() const;
	virtual bool isTransparent();
	virtual bool supportsAdvancedTransparency() const;
	virtual bool overridesDrawState();
	virtual bool rebuildAlways();
	virtual double boundingBoxExtraScale() const;

private:
	dx11ShaderOverride(const MObject& obj);

	// Current dx11Shader node associated with the shader override.
	dx11ShaderNode* fShaderNode;
	// version id which gets synced with match var on effect
	size_t			fGeometryVersionId;

	//
	mutable double fBBoxExtraScale;

	// States values to save before and restore after executing the shader
	dx11ShaderNode::ContextStates fStates;
};

#endif /* _dx11ShaderOverride_h_ */
