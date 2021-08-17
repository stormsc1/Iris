include "dependencies.lua"

workspace "Iris"
	architecture "x86_64"
	startproject "Iris"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

include "Iris"
include "IrisSandbox"


