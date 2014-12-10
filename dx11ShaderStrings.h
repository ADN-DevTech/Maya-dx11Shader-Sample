#ifndef __dx11ShaderStrings_h__
#define __dx11ShaderStrings_h__

#include <maya/MStringResourceId.h>

class MString;
class MStringArray;

namespace dx11ShaderStrings
{
	// dx11Shader_initUI.mel
	extern const MStringResourceId kReloadTool;
	extern const MStringResourceId kReloadAnnotation;
	extern const MStringResourceId kEditTool;
	extern const MStringResourceId kEditAnnotationWIN;
	extern const MStringResourceId kEditAnnotationMAC;
	extern const MStringResourceId kEditAnnotationLNX;
	extern const MStringResourceId kEditCommandLNX;
	extern const MStringResourceId kNiceNodeName;

	// AEdx11ShaderTemplate.mel
	extern const MStringResourceId kShaderFile;
	extern const MStringResourceId kShader;
	extern const MStringResourceId kTechnique;
	extern const MStringResourceId kTechniqueTip;
	extern const MStringResourceId kLightBinding;
	extern const MStringResourceId kLightConnectionNone;
	extern const MStringResourceId kLightConnectionImplicit;
	extern const MStringResourceId kLightConnectionImplicitNone;
	extern const MStringResourceId kLightConnectionNoneTip;
	extern const MStringResourceId kLightConnectionImplicitTip;
	extern const MStringResourceId kLightConnectionExplicitTip;
	extern const MStringResourceId kShaderParameters;
	extern const MStringResourceId kSurfaceData;
	extern const MStringResourceId kDiagnostics;
	extern const MStringResourceId kNoneDefined;
	extern const MStringResourceId kEffectFiles;
	extern const MStringResourceId kAmbient;

	extern const MStringResourceId kActionChoose;
	extern const MStringResourceId kActionEdit;
	extern const MStringResourceId kActionNew;
	extern const MStringResourceId kActionNewAnnotation;
	extern const MStringResourceId kActionNothing;
	extern const MStringResourceId kActionNothingAnnotation;
	extern const MStringResourceId kActionNotAssigned;
	extern const MStringResourceId kActionEmptyCommand;

	extern const MStringResourceId kActionEditorTitle;
	extern const MStringResourceId kActionEditorName;
	extern const MStringResourceId kActionEditorImageFile;
	extern const MStringResourceId kActionEditorDescription;
	extern const MStringResourceId kActionEditorCommands;
	extern const MStringResourceId kActionEditorInsertVariable;
	extern const MStringResourceId kActionEditorSave;
	extern const MStringResourceId kActionEditorCreate;
	extern const MStringResourceId kActionEditorDelete;
	extern const MStringResourceId kActionEditorClose;

	//dx11ShaderCreateUI.mel
	extern const MStringResourceId kPrefSavePrefsOrNotMsg;
	extern const MStringResourceId kPrefSaveMsg;
	extern const MStringResourceId kPrefFileOpen;
	extern const MStringResourceId kPrefDefaultEffectFile;
	extern const MStringResourceId kPrefDx11ShaderPanel;
	extern const MStringResourceId kPrefDx11ShaderTab;

	//dx11ShaderCompileHelper
	extern const MStringResourceId kErrorEffectCompile;
	extern const MStringResourceId kErrorEffectBuffer;
	extern const MStringResourceId kErrorFileNotFound;
	extern const MStringResourceId kErrorInvalidEffectFile;
	extern const MStringResourceId kErrorAbsolutePathNotFound;

	//dx11ShaderNode
	extern const MStringResourceId kErrorIndexVaryingParameter;
	extern const MStringResourceId kErrorVertexRequirement;
	extern const MStringResourceId kWarningVertexRequirement;

	extern const MStringResourceId kErrorNoValidTechniques;
	extern const MStringResourceId kErrorSetTechniqueByName;
	extern const MStringResourceId kErrorSetTechniqueByIndex;
	extern const MStringResourceId kErrorIsTransparentOpacity;
	extern const MStringResourceId kErrorUnknownIsTransparent;
	extern const MStringResourceId kErrorSetPass;
	extern const MStringResourceId kErrorRegisterNodeType;
	extern const MStringResourceId kErrorDeregisterNodeType;

	extern const MStringResourceId kErrorLog;
	extern const MStringResourceId kWarningLog;

	//dx11ShaderCmd
	extern const MStringResourceId kInvalidDx11ShaderNode;
	extern const MStringResourceId kUnknownConnectableLight;
	extern const MStringResourceId kUnknownSceneObject;
	extern const MStringResourceId kUnknownUIGroup;
	extern const MStringResourceId kNotALight;

	//dx11ShaderUniformParamBuilder
	extern const MStringResourceId kUnsupportedType;
	extern const MStringResourceId kUnknowSemantic;
	extern const MStringResourceId kUnknowSpace;
	extern const MStringResourceId kNoTextureType;
	extern const MStringResourceId kUnknownTextureSemantic;
	
	extern const MStringResourceId kTypeStringVector;
	extern const MStringResourceId kTypeBoolVector;
	extern const MStringResourceId kTypeIntVector;
	extern const MStringResourceId kTypeIntUInt8;
	extern const MStringResourceId kTypeDouble;
	extern const MStringResourceId kTypeTextureArray;

	//dx11ConeAngleToHotspotConverter
	extern const MStringResourceId kErrorConeAngle;
	extern const MStringResourceId kErrorPenumbraAngle;

	// Register all strings
	MStatus registerMStringResources(void);

	MString getString(const MStringResourceId &stringId);
	MString getString(const MStringResourceId &stringId, const MString& arg);
	MString getString(const MStringResourceId &stringId, const MStringArray& args);
}

#endif

//-
// Copyright 2012 Autodesk, Inc.  All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk license agreement
// provided at the time of installation or download, or which otherwise
// accompanies this software in either electronic or hard copy form.
//+

