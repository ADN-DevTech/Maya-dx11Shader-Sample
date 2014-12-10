#include "dx11ShaderStrings.h"

#include <maya/MStringResource.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>

namespace dx11ShaderStrings
{
	#define kPluginId  "dx11Shader"

	// dx11Shader_initUI.mel
	const MStringResourceId kReloadTool					( kPluginId, "kReloadTool", 				MString( "Reload" ) );
	const MStringResourceId kReloadAnnotation			( kPluginId, "kReloadAnnotation", 			MString( "Reload fx file" ) );
	const MStringResourceId kEditTool					( kPluginId, "kEditTool", 					MString( "Edit" ) );
	const MStringResourceId kEditAnnotationWIN			( kPluginId, "kEditAnnotationWIN", 			MString( "Edit with application associated with fx file" ) );
	const MStringResourceId kEditAnnotationMAC			( kPluginId, "kEditAnnotationMAC", 			MString( "Edit fx file with TextEdit" ) );
	const MStringResourceId kEditAnnotationLNX			( kPluginId, "kEditAnnotationLNX", 			MString( "Edit fx file with your editor" ) );
	const MStringResourceId kEditCommandLNX				( kPluginId, "kEditCommandLNX", 			MString( "Please set the command before using this feature." ) );
	const MStringResourceId kNiceNodeName				( kPluginId, "kNiceNodeName", 				MString( "DirectX 11 Shader" ) );

	// AEdx11ShaderTemplate.mel
	const MStringResourceId kShaderFile					( kPluginId, "kShaderFile", 				MString( "Shader File" ) );
	const MStringResourceId kShader						( kPluginId, "kShader", 					MString( "Shader" ) );
	const MStringResourceId kTechnique					( kPluginId, "kTechnique", 					MString( "Technique" ) );
	const MStringResourceId kTechniqueTip				( kPluginId, "kTechniqueTip", 				MString( "Select among the rendering techniques defined in the fx file." ) );
	const MStringResourceId kLightBinding				( kPluginId, "kLightBinding",				MString( "Light Binding" ) );
	const MStringResourceId kLightConnectionNone		( kPluginId, "kLightConnectionNone", 		MString( "Use Shader Settings" ) );
	const MStringResourceId kLightConnectionImplicit	( kPluginId, "kLightConnectionImplicit", 	MString( "Automatic Bind" ) );
	const MStringResourceId kLightConnectionImplicitNone( kPluginId, "kLightConnectionImplicitNone",MString( "none" ) );
	const MStringResourceId kLightConnectionNoneTip		( kPluginId, "kLightConnectionNoneTip", 	MString( "Ignores Maya lights and uses the settings in the fx file." ) );
	const MStringResourceId kLightConnectionImplicitTip	( kPluginId, "kLightConnectionImplicitTip", MString( "Maya will automatically bind scene lights and parameters to the lights defined in the fx file." ) );
	const MStringResourceId kLightConnectionExplicitTip	( kPluginId, "kLightConnectionExplicitTip",	MString( "Explicitly binds a Maya scene light and it's parameters to a light defined in the fx file." ) );
	const MStringResourceId kShaderParameters			( kPluginId, "kShaderParameters", 			MString( "Parameters" ) );
	const MStringResourceId kSurfaceData				( kPluginId, "kSurfaceData", 				MString( "Surface Data" ) );
	const MStringResourceId kDiagnostics				( kPluginId, "kDiagnostics", 				MString( "Diagnostics" ) );
	const MStringResourceId kNoneDefined				( kPluginId, "kNoneDefined", 				MString( "None defined" ) );
	const MStringResourceId kEffectFiles				( kPluginId, "kEffectFiles", 				MString( "Effect Files" ) );
	const MStringResourceId kAmbient					( kPluginId, "kAmbient", 					MString( "Ambient" ) );

	const MStringResourceId kActionChoose				( kPluginId, "kActionChoose",				MString( "Choose button action:" ) );
	const MStringResourceId kActionEdit					( kPluginId, "kActionEdit", 				MString( "Edit the action definition" ) );
	const MStringResourceId kActionNew					( kPluginId, "kActionNew", 					MString( "New action..." ) );
	const MStringResourceId kActionNewAnnotation		( kPluginId, "kActionNewAnnotation", 		MString( "Add a new action to this menu" ) );
	const MStringResourceId kActionNothing				( kPluginId, "kActionNothing", 				MString( "(nothing)" ) );
	const MStringResourceId kActionNothingAnnotation	( kPluginId, "kActionNothingAnnotation", 	MString( "Button does nothing; not assigned." ) );
	const MStringResourceId kActionNotAssigned			( kPluginId, "kActionNotAssigned", 			MString( "LMB: Does nothing; not assigned.  RMB: Choose what this button should do." ) );
	const MStringResourceId kActionEmptyCommand			( kPluginId, "kActionEmptyCommand", 		MString( "LMB: Does nothing; empty command.  RMB: Choose what this button should do." ) );

	const MStringResourceId kActionEditorTitle			( kPluginId, "kActionEditorTitle", 			MString( "dx11Shader Tool Button Editor" ) );
	const MStringResourceId kActionEditorName			( kPluginId, "kActionEditorName", 			MString( "Name" ) );
	const MStringResourceId kActionEditorImageFile		( kPluginId, "kActionEditorImageFile", 		MString( "Image File" ) );
	const MStringResourceId kActionEditorDescription	( kPluginId, "kActionEditorDescription", 	MString( "Description" ) );
	const MStringResourceId kActionEditorCommands		( kPluginId, "kActionEditorCommands", 		MString( "Commands" ) );
	const MStringResourceId kActionEditorInsertVariable	( kPluginId, "kActionEditorInsertVariable", MString( "Insert variable:" ) );
	const MStringResourceId kActionEditorSave			( kPluginId, "kActionEditorSave", 			MString( "Save" ) );
	const MStringResourceId kActionEditorCreate			( kPluginId, "kActionEditorCreate", 		MString( "Create" ) );
	const MStringResourceId kActionEditorDelete			( kPluginId, "kActionEditorDelete", 		MString( "Delete" ) );
	const MStringResourceId kActionEditorClose			( kPluginId, "kActionEditorClose", 			MString( "Close" ) );

	//dx11ShaderCreateUI.mel
	const MStringResourceId kPrefSavePrefsOrNotMsg		( kPluginId, "kPrefSavePrefsOrNotMsg", 		MString( "The dx11Shader plug-in is about to refresh preferences windows. Save your changes in the preferences window?" ) );
	const MStringResourceId kPrefSaveMsg				( kPluginId, "kPrefSaveMsg", 				MString( "Save Preferences" ) );
	const MStringResourceId kPrefFileOpen				( kPluginId, "kPrefFileOpen", 				MString( "Open" ) );
	const MStringResourceId kPrefDefaultEffectFile		( kPluginId, "kPrefDefaultEffectFile", 		MString( "Default effects file" ) );
	const MStringResourceId kPrefDx11ShaderPanel		( kPluginId, "kPrefDx11ShaderPanel", 		MString( "DX 11 Shader Preferences" ) );
	const MStringResourceId kPrefDx11ShaderTab			( kPluginId, "kPrefDx11ShaderTab", 			MString( "    DX 11 Shader" ) );

	//dx11ShaderCompileHelper
	const MStringResourceId kErrorEffectCompile			( kPluginId, "kErrorEffectCompile", 		MString( "Effect from file compile errors (^1s) {\n^2s\n}\n" ) );
	const MStringResourceId kErrorEffectBuffer			( kPluginId, "kErrorEffectBuffer", 			MString( "Effect from buffer creation errors: {\n^1s\n}\n" ) );
	const MStringResourceId kErrorFileNotFound			( kPluginId, "kErrorFileNotFound", 			MString( "Effect file \"^1s\" not found." ) );
	const MStringResourceId kErrorInvalidEffectFile		( kPluginId, "kErrorInvalidEffectFile",		MString( "Invalid effect file \"^1s\"." ) );
	const MStringResourceId kErrorAbsolutePathNotFound	( kPluginId, "kErrorAbsolutePathNotFound",	MString( "Effect file \"^1s\" not found, using \"^2s\" instead.\n" ) );

	//dx11ShaderNode
	const MStringResourceId kErrorIndexVaryingParameter	( kPluginId, "kErrorIndexVaryingParameter",	MString( "Unsupported index on varying parameter ^1s. Index will be interpreted as 0\n" ) );
	const MStringResourceId kErrorVertexRequirement		( kPluginId, "kErrorVertexRequirement", 	MString( "Unsupported per vertex requirement for ^1s^2s. The vector size should be between ^3s and ^4s, the effect requires ^5s\n" ) );
	const MStringResourceId kWarningVertexRequirement	( kPluginId, "kWarningVertexRequirement", 	MString( "Per vertex requirement for ^1s. The vector size maya provides is ^2s, the effect requires ^3s\n" ) );

	const MStringResourceId kErrorNoValidTechniques		( kPluginId, "kErrorNoValidTechniques", 	MString( "Cannot initialize techniques. Effect has no valid techniques.\n" ) );
	const MStringResourceId kErrorSetTechniqueByName	( kPluginId, "kErrorSetTechniqueByName",	MString( "Failed to set technique. Technique ^1s does not exist on effect.\n" ) );
	const MStringResourceId kErrorSetTechniqueByIndex	( kPluginId, "kErrorSetTechniqueByIndex",	MString( "Failed to set technique. Technique #^1s does not exist on effect.\n" ) );
	const MStringResourceId kErrorIsTransparentOpacity	( kPluginId, "kErrorIsTransparentOpacity", 	MString( "Technique with isTransparent == 2 annotation must have one parameter with Opacity semantics.\n" ) );
	const MStringResourceId kErrorUnknownIsTransparent	( kPluginId, "kErrorUnknownIsTransparent", 	MString( "Technique has unknown isTransparent annotation value ( 0: Opaque, 1: Transparent, 2: Use opacity semantics ).\n" ) );
	const MStringResourceId kErrorSetPass				( kPluginId, "kErrorSetPass", 				MString( "Failed to set pass. Pass #^1s does not exist on technique: ^2s.\n" ) );
	const MStringResourceId kErrorRegisterNodeType		( kPluginId, "kErrorRegisterNodeType",		MString( "Failed to register ^1s to filePathEditor" ) );
	const MStringResourceId kErrorDeregisterNodeType	( kPluginId, "kErrorDeregisterNodeType",	MString( "Failed to deregister ^1s from filePathEditor" ) );
	
	const MStringResourceId kErrorLog					( kPluginId, "kErrorLog", 					MString( "errors :\n^1s" ) );
	const MStringResourceId kWarningLog					( kPluginId, "kWarningLog", 				MString( "warnings :\n^1s" ) );

	//dx11ShaderCmd
	const MStringResourceId kInvalidDx11ShaderNode		( kPluginId, "kInvalidDx11ShaderNode",		MString( "Invalid dx11Shader node: ^1s" ) );
	const MStringResourceId kUnknownConnectableLight	( kPluginId, "kUnknownConnectableLight",	MString( "Unknown connectable light: ^1s" ) );
	const MStringResourceId kUnknownSceneObject			( kPluginId, "kUnknownSceneObject",			MString( "Unknown scene object: ^1s" ) );
	const MStringResourceId kUnknownUIGroup				( kPluginId, "kUnknownUIGroup",				MString( "Unknown UI group: ^1s" ) );
	const MStringResourceId kNotALight					( kPluginId, "kNotALight",					MString( "Not a light: ^1s" ) );

	//dx11ShaderUniformParamBuilder
	const MStringResourceId kUnsupportedType			( kPluginId, "kUnsupportedType",			MString( "Unsupported ^1s on parameter ^2s. Parameter will be ignored\n" ) );
	const MStringResourceId kUnknowSemantic				( kPluginId, "kUnknowSemantic",				MString( "Unrecognized semantic ^1s on parameter ^2s\n" ) );
	const MStringResourceId kUnknowSpace				( kPluginId, "kUnknowSpace",				MString( "Unrecognised space ^1s on parameter ^2s\n" ) );
	const MStringResourceId kNoTextureType				( kPluginId, "kNoTextureType",				MString( "No texture type provided for ^1s, assuming 2D\n" ) );
	const MStringResourceId kUnknownTextureSemantic		( kPluginId, "kUnknownTextureSemantic",		MString( "Unrecognised texture type semantic ^1s on parameter ^2s\n" ) );

	const MStringResourceId kTypeStringVector			( kPluginId, "kTypeStringVector", 			MString( "string vector" ) );
	const MStringResourceId kTypeBoolVector				( kPluginId, "kTypeBoolVector", 			MString( "bool vector" ) );
	const MStringResourceId kTypeIntVector				( kPluginId, "kTypeIntVector", 				MString( "int vector" ) );
	const MStringResourceId kTypeIntUInt8				( kPluginId, "kTypeIntUInt8", 				MString( "uint8" ) );
	const MStringResourceId kTypeDouble					( kPluginId, "kTypeDouble", 				MString( "double" ) );
	const MStringResourceId kTypeTextureArray			( kPluginId, "kTypeTextureArray", 			MString( "texture array" ) );

	//dx11ConeAngleToHotspotConverter
	const MStringResourceId kErrorConeAngle				( kPluginId, "kErrorConeAngle", 			MString( "ERROR getting ConeAngle data" ) );
	const MStringResourceId kErrorPenumbraAngle			( kPluginId, "kErrorPenumbraAngle",			MString( "ERROR getting PenumbraAngle data" ) );
}


// Register all strings
//
MStatus dx11ShaderStrings::registerMStringResources(void)
{
	// dx11Shader_initUI.mel
	MStringResource::registerString( kReloadTool );
	MStringResource::registerString( kReloadAnnotation );
	MStringResource::registerString( kEditTool );
	MStringResource::registerString( kEditAnnotationWIN );
	MStringResource::registerString( kEditAnnotationMAC );
	MStringResource::registerString( kEditAnnotationLNX );
	MStringResource::registerString( kEditCommandLNX );
	MStringResource::registerString( kNiceNodeName );

	// AEdx11ShaderTemplate.mel
	MStringResource::registerString( kShaderFile );
	MStringResource::registerString( kShader );
	MStringResource::registerString( kTechnique );
	MStringResource::registerString( kTechniqueTip );
	MStringResource::registerString( kLightBinding );
	MStringResource::registerString( kLightConnectionNone );
	MStringResource::registerString( kLightConnectionImplicit );
	MStringResource::registerString( kLightConnectionImplicitNone );
	MStringResource::registerString( kLightConnectionNoneTip );
	MStringResource::registerString( kLightConnectionImplicitTip );
	MStringResource::registerString( kLightConnectionExplicitTip );
	MStringResource::registerString( kShaderParameters );
	MStringResource::registerString( kSurfaceData );
	MStringResource::registerString( kDiagnostics );
	MStringResource::registerString( kNoneDefined );
	MStringResource::registerString( kEffectFiles );
	MStringResource::registerString( kAmbient );

	MStringResource::registerString( kActionChoose );
	MStringResource::registerString( kActionEdit );
	MStringResource::registerString( kActionNew );
	MStringResource::registerString( kActionNewAnnotation );
	MStringResource::registerString( kActionNothing );
	MStringResource::registerString( kActionNothingAnnotation );
	MStringResource::registerString( kActionNotAssigned );
	MStringResource::registerString( kActionEmptyCommand );

	MStringResource::registerString( kActionEditorTitle );
	MStringResource::registerString( kActionEditorName );
	MStringResource::registerString( kActionEditorImageFile );
	MStringResource::registerString( kActionEditorDescription );
	MStringResource::registerString( kActionEditorCommands );
	MStringResource::registerString( kActionEditorInsertVariable );
	MStringResource::registerString( kActionEditorSave );
	MStringResource::registerString( kActionEditorCreate );
	MStringResource::registerString( kActionEditorDelete );
	MStringResource::registerString( kActionEditorClose );

	//dx11ShaderCreateUI.mel
	MStringResource::registerString( kPrefSavePrefsOrNotMsg );
	MStringResource::registerString( kPrefSaveMsg );
	MStringResource::registerString( kPrefFileOpen );
	MStringResource::registerString( kPrefDefaultEffectFile );
	MStringResource::registerString( kPrefDx11ShaderPanel );
	MStringResource::registerString( kPrefDx11ShaderTab );

	//dx11ShaderCompileHelper
	MStringResource::registerString( kErrorEffectCompile );
	MStringResource::registerString( kErrorEffectBuffer );
	MStringResource::registerString( kErrorFileNotFound );
	MStringResource::registerString( kErrorAbsolutePathNotFound );

	//dx11ShaderNode
	MStringResource::registerString( kErrorIndexVaryingParameter );
	MStringResource::registerString( kErrorVertexRequirement );
	MStringResource::registerString( kWarningVertexRequirement );

	MStringResource::registerString( kErrorNoValidTechniques );
	MStringResource::registerString( kErrorSetTechniqueByName );
	MStringResource::registerString( kErrorSetTechniqueByIndex );
	MStringResource::registerString( kErrorIsTransparentOpacity );
	MStringResource::registerString( kErrorUnknownIsTransparent );
	MStringResource::registerString( kErrorSetPass );
	MStringResource::registerString( kErrorRegisterNodeType );
	MStringResource::registerString( kErrorDeregisterNodeType );

	MStringResource::registerString( kErrorLog );
	MStringResource::registerString( kWarningLog );

	//dx11ShaderCmd
	MStringResource::registerString( kInvalidDx11ShaderNode );
	MStringResource::registerString( kUnknownConnectableLight );
	MStringResource::registerString( kUnknownSceneObject );
	MStringResource::registerString( kUnknownUIGroup );
	MStringResource::registerString( kNotALight );

	//dx11ShaderUniformParamBuilder
	MStringResource::registerString( kUnsupportedType );
	MStringResource::registerString( kUnknowSemantic );
	MStringResource::registerString( kUnknowSpace );
	MStringResource::registerString( kNoTextureType );
	MStringResource::registerString( kUnknownTextureSemantic );

	MStringResource::registerString( kTypeStringVector );
	MStringResource::registerString( kTypeBoolVector );
	MStringResource::registerString( kTypeIntVector );
	MStringResource::registerString( kTypeIntUInt8 );
	MStringResource::registerString( kTypeDouble );
	MStringResource::registerString( kTypeTextureArray );

	//dx11ConeAngleToHotspotConverter
	MStringResource::registerString( kErrorConeAngle );
	MStringResource::registerString( kErrorPenumbraAngle );

	return MS::kSuccess;
}

MString dx11ShaderStrings::getString(const MStringResourceId &stringId)
{
	MStatus status;
	return MStringResource::getString(stringId, status);
}

MString dx11ShaderStrings::getString(const MStringResourceId &stringId, const MString& arg)
{
	MStringArray args;
	args.append(arg);
	return dx11ShaderStrings::getString(stringId, args);
}

MString dx11ShaderStrings::getString(const MStringResourceId &stringId, const MStringArray& args)
{
	MString string;
	string.format( dx11ShaderStrings::getString(stringId), args);
	return string;
}

//-
// Copyright 2012 Autodesk, Inc.  All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk license agreement
// provided at the time of installation or download, or which otherwise
// accompanies this software in either electronic or hard copy form.
//+
