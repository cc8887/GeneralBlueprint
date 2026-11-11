// Copyright (c) 2026 cc8887. All Rights Reserved.
#pragma once

#include "AnimBP2FPVersionCompat.h"

#if WITH_EDITOR && ANIMBP2FP_HAS_MODERN_RIGVM_AUTHORING
#include "Rigs/RigHierarchy.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"

namespace RigLangEngineCompat
{
inline FString FunctionHostPath(const FRigVMGraphFunctionIdentifier& Identifier)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 9)
	return Identifier.GetHostPath().ToString();
#else
	return Identifier.HostObject.ToString();
#endif
}

inline FString FunctionLibraryNodePath(const FRigVMGraphFunctionIdentifier& Identifier)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 9)
	return Identifier.GetLibraryNodePath().ToString();
#else
	return Identifier.GetLibraryNodePath();
#endif
}

inline FString ParentLabel(const URigHierarchy* Hierarchy, const FRigMultiParentElement* Element, int32 ParentIndex)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 9)
	return Hierarchy->GetRigElementParentConstraintsTopology(Element)[ParentIndex].GetDisplayLabel().ToString();
#else
	return Element->ParentConstraints[ParentIndex].DisplayLabel.ToString();
#endif
}

inline FRigControlSettings ControlSettings(const URigHierarchy* Hierarchy, const FRigControlElement* Control)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 9)
	return Hierarchy->MakeControlSettings(Control);
#else
	return Control->Settings;
#endif
}

inline FTransform ControlOffset(const URigHierarchy* Hierarchy, const FRigControlElement* Control,
	ERigTransformType::Type TransformType)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 9)
	return Hierarchy->GetControlOffsetTransform(Control, TransformType);
#else
	return Control->GetOffsetTransform()[TransformType].Get();
#endif
}
}
#endif
