project "IrisSandbox"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp"
	}

	includedirs
	{
		"src",
		"%{wks.location}/Iris/src",
		"%{IncludeDir.asio}"
	}

	links
	{
		"Iris"
	}

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		defines "IRIS_DEBUG"
		runtime "Debug"
		symbols "on"


	filter "configurations:Release"
		defines "IRIS_RELEASE"
		runtime "Release"
		optimize "on"

	
	filter "configurations:Dist"
		defines "IRIS_DIST"
		runtime "Release"
		optimize "on"