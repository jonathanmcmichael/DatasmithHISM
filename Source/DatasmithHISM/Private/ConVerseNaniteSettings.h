#pragma once

#include "Engine/StaticMesh.h"
#include "Misc/EngineVersionComparison.h"

inline FMeshNaniteSettings& ConVerseGetNaniteSettings(UStaticMesh& Mesh)
{
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	return Mesh.NaniteSettings;
#else
	return Mesh.GetNaniteSettings();
#endif
}

inline const FMeshNaniteSettings& ConVerseGetNaniteSettings(const UStaticMesh& Mesh)
{
#if UE_VERSION_OLDER_THAN(5, 7, 0)
	return Mesh.NaniteSettings;
#else
	return Mesh.GetNaniteSettings();
#endif
}
