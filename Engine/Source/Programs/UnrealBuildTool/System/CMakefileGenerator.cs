﻿// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.IO;

namespace UnrealBuildTool
{
	/// <summary>
	/// Represents a folder within the master project (e.g. Visual Studio solution)
	/// </summary>
	public class CMakefileFolder : MasterProjectFolder
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public CMakefileFolder(ProjectFileGenerator InitOwnerProjectFileGenerator, string InitFolderName)
			: base(InitOwnerProjectFileGenerator, InitFolderName)
		{
		}
	}

	public class CMakefileProjectFile : ProjectFile
	{
		public CMakefileProjectFile(FileReference InitFilePath)
			: base(InitFilePath)
		{
		}
	}
	/// <summary>
	/// CMakefile project file generator implementation
	/// </summary>
	public class CMakefileGenerator : ProjectFileGenerator
	{
		/// <summary>
		/// Creates a new instance of the <see cref="CMakefileGenerator"/> class.
		/// </summary>
		public CMakefileGenerator(FileReference InOnlyGameProject)
			: base(InOnlyGameProject)
		{
		}

		/// <summary>
		/// Determines whether or not we should generate code completion data whenever possible.
		/// </summary>
		/// <returns><value>true</value> if we should generate code completion data; <value>false</value> otherwise.</returns>
		public override bool ShouldGenerateIntelliSenseData()
		{
			return true;
		}

		/// <summary>
		/// The file extension for this project file.
		/// </summary>
		public override string ProjectFileExtension
		{
			get
			{
				return ".txt";
			}
		}

		public string ProjectFileName
		{
			get
			{
				return "CMakeLists" + ProjectFileExtension;
			}
		}

		/// <summary>
		/// Writes the master project file (e.g. Visual Studio Solution file)
		/// </summary>
		/// <param name="UBTProject">The UnrealBuildTool project</param>
		/// <returns>True if successful</returns>
		protected override bool WriteMasterProjectFile(ProjectFile UBTProject)
		{
			return true;
		}

		private bool WriteCMakeLists()
		{
			string BuildCommand;
			const string CMakeSectionEnd = " )\n\n";

			var CMakefileContent = new StringBuilder();

			StringBuilder CMakeSourceFilesList = new StringBuilder("set(SOURCE_FILES \n");
			StringBuilder CMakeHeaderFilesList = new StringBuilder("set(HEADER_FILES \n");
			StringBuilder CMakeConfigFilesList = new StringBuilder("set(CONFIG_FILES \n");
			StringBuilder IncludeDirectoriesList = new StringBuilder("include_directories( \n");
			StringBuilder PreprocessorDefinitionsList = new StringBuilder("add_definitions( \n");

			var CMakeGameRootPath = "";
			var CMakeUE4RootPath = $"set(UE4_ROOT_PATH \"{Utils.CleanDirectorySeparators(Path.GetFullPath(RootRelativePath), '/')}\")\n";

			string GameProjectPath = "";
			string CMakeGameProjectFile = "";

			string HostArchitecture;
			switch (BuildHostPlatform.Current.Platform)
			{
				case UnrealTargetPlatform.Win64:
				{
					HostArchitecture = "Win64";
					BuildCommand = "set(BUILD cmd /c \"${UE4_ROOT_PATH}/Engine/Build/BatchFiles/Build.bat\")\n";
					break;
				}
				case UnrealTargetPlatform.Mac:
				{
					HostArchitecture = "Mac";
					BuildCommand = $"set(BUILD cd \"${{UE4_ROOT_PATH}}\" && bash \"${{UE4_ROOT_PATH}}/Engine/Build/BatchFiles/{HostArchitecture}/Build.sh\")\n";
					break;
				}
				case UnrealTargetPlatform.Linux:
				{
					HostArchitecture = "Linux";
					BuildCommand = $"set(BUILD cd \"${{UE4_ROOT_PATH}}\" && bash \"${{UE4_ROOT_PATH}}/Engine/Build/BatchFiles/{HostArchitecture}/Build.sh\")\n";
					break;
				}
				default:
				{
					throw new BuildException("ERROR: CMakefileGenerator does not support this platform");
				}
			}

			if (!String.IsNullOrEmpty(GameProjectName))
			{
				GameProjectPath = OnlyGameProject.Directory.FullName;

				CMakeGameRootPath = $"set(GAME_ROOT_PATH \"{Utils.CleanDirectorySeparators(OnlyGameProject.Directory.FullName, '/')}\")\n";
				CMakeGameProjectFile = $"set(GAME_PROJECT_FILE \"{Utils.CleanDirectorySeparators(OnlyGameProject.FullName, '/')}\")\n";
			}

			CMakefileContent.Append(
				"# Makefile generated by CMakefileGenerator.cs (v1.1)\n" +
				"# *DO NOT EDIT*\n\n" +
				"cmake_minimum_required (VERSION 2.6)\n" +
				"project (UE4)\n\n" +
				CMakeUE4RootPath +
				CMakeGameProjectFile +
				BuildCommand +
				CMakeGameRootPath + "\n"
			);

			List<string> IncludeDirectories = new List<string>();
			List<string> PreprocessorDefinitions = new List<string>();

			foreach (var CurProject in GeneratedProjectFiles)
			{
				foreach (var IncludeSearchPath in CurProject.IntelliSenseIncludeSearchPaths)
				{
					string IncludeDirectory = GetIncludeDirectory(IncludeSearchPath, Path.GetDirectoryName(CurProject.ProjectFilePath.FullName));
					if (IncludeDirectory != null && !IncludeDirectories.Contains(IncludeDirectory))
					{
						if (IncludeDirectory.Contains(Path.GetFullPath(RootRelativePath)))
						{
							IncludeDirectories.Add(IncludeDirectory.Replace(Path.GetFullPath(RootRelativePath), "${UE4_ROOT_PATH}"));
						}
						else
						{
							IncludeDirectories.Add($"${{GAME_ROOT_PATH}}/{IncludeDirectory}");
						}
					}
				}

				foreach (var PreProcessorDefinition in CurProject.IntelliSensePreprocessorDefinitions)
				{
					string Definition = PreProcessorDefinition;
					string AlternateDefinition = Definition.Contains("=0") ? Definition.Replace("=0", "=1") : Definition.Replace("=1", "=0");

					if (Definition.Equals("WITH_EDITORONLY_DATA=0") || Definition.Equals("WITH_DATABASE_SUPPORT=1"))
					{
						Definition = AlternateDefinition;
					}

					if (!PreprocessorDefinitions.Contains(Definition) &&
						!PreprocessorDefinitions.Contains(AlternateDefinition) &&
						!Definition.StartsWith("UE_ENGINE_DIRECTORY") &&
						!Definition.StartsWith("ORIGINAL_FILE_NAME"))
					{
						PreprocessorDefinitions.Add(Definition);
					}
				}
			}

			// Create SourceFiles, HeaderFiles, and ConfigFiles sections.
			var AllModuleFiles = DiscoverModules(FindGameProjects());
			foreach (FileReference CurModuleFile in AllModuleFiles)
			{
				var FoundFiles = SourceFileSearch.FindModuleSourceFiles(CurModuleFile);
				foreach (FileReference CurSourceFile in FoundFiles)
				{
					string SourceFileRelativeToRoot = CurSourceFile.MakeRelativeTo(UnrealBuildTool.EngineDirectory);

					// Exclude files/folders on a per-platform basis.
					if (!IsPathExcludedOnPlatform(SourceFileRelativeToRoot, BuildHostPlatform.Current.Platform))
					{
						if (SourceFileRelativeToRoot.EndsWith(".cpp"))
						{
							if (!SourceFileRelativeToRoot.StartsWith("..") && !Path.IsPathRooted(SourceFileRelativeToRoot))
							{
								CMakeSourceFilesList.Append($"\t\"${{UE4_ROOT_PATH}}/Engine/{Utils.CleanDirectorySeparators(SourceFileRelativeToRoot, '/')}\"\n");
							}
							else
							{
								if (String.IsNullOrEmpty(GameProjectName))
								{
									CMakeSourceFilesList.Append($"\t\"{Utils.CleanDirectorySeparators(SourceFileRelativeToRoot, '/').Substring(3)}\"\n");
								}
								else
								{
									string relativeGameSourcePath = Utils.MakePathRelativeTo(CurSourceFile.FullName, GameProjectPath);
									CMakeSourceFilesList.Append($"\t\"${{GAME_ROOT_PATH}}/{Utils.CleanDirectorySeparators(relativeGameSourcePath, '/')}\"\n");
								}
							}
						}
						else if (SourceFileRelativeToRoot.EndsWith(".h"))
						{
							if (!SourceFileRelativeToRoot.StartsWith("..") && !Path.IsPathRooted(SourceFileRelativeToRoot))
							{
								CMakeHeaderFilesList.Append($"\t\"${{UE4_ROOT_PATH}}/Engine/{Utils.CleanDirectorySeparators(SourceFileRelativeToRoot, '/')}\"\n");
							}
							else
							{
								if (String.IsNullOrEmpty(GameProjectName))
								{
									CMakeHeaderFilesList.Append($"\t\"{Utils.CleanDirectorySeparators(SourceFileRelativeToRoot, '/').Substring(3)}\"\n");
								}
								else
								{
									string relativeGameSourcePath = Utils.MakePathRelativeTo(CurSourceFile.FullName, GameProjectPath);
									CMakeHeaderFilesList.Append($"\t\"${{GAME_ROOT_PATH}}/{Utils.CleanDirectorySeparators(relativeGameSourcePath, '/')}\"\n");
								}
							}
						}
						else if (SourceFileRelativeToRoot.EndsWith(".cs"))
						{
							if (!SourceFileRelativeToRoot.StartsWith("..") && !Path.IsPathRooted(SourceFileRelativeToRoot))
							{
								CMakeConfigFilesList.Append($"\t\"${{UE4_ROOT_PATH}}/Engine/{Utils.CleanDirectorySeparators(SourceFileRelativeToRoot, '/')}\"\n");

							}
							else
							{
								if (String.IsNullOrEmpty(GameProjectName))
								{
									CMakeConfigFilesList.Append($"\t\"{Utils.CleanDirectorySeparators(SourceFileRelativeToRoot, '/').Substring(3)}\"\n");
								}
								else
								{
									string relativeGameSourcePath = Utils.MakePathRelativeTo(CurSourceFile.FullName, GameProjectPath);
									CMakeConfigFilesList.Append($"\t\"${{GAME_ROOT_PATH}}/{Utils.CleanDirectorySeparators(relativeGameSourcePath, '/')}\"\n");
								}
							}
						}
					}
				}

			}

			foreach (string IncludeDirectory in IncludeDirectories)
			{
				IncludeDirectoriesList.Append($"\t\"{Utils.CleanDirectorySeparators(IncludeDirectory, '/')}\"\n");
			}

			foreach (string PreprocessorDefinition in PreprocessorDefinitions)
			{
				PreprocessorDefinitionsList.Append($"\t-D{PreprocessorDefinition}\n");
			}

			// Add section end to section strings;
			CMakeSourceFilesList.Append(CMakeSectionEnd);
			CMakeHeaderFilesList.Append(CMakeSectionEnd);
			CMakeConfigFilesList.Append(CMakeSectionEnd);
			IncludeDirectoriesList.Append(CMakeSectionEnd);
			PreprocessorDefinitionsList.Append(CMakeSectionEnd);

			// Append sections to the CMakeLists.txt file
			CMakefileContent.Append(CMakeSourceFilesList);
			CMakefileContent.Append(CMakeHeaderFilesList);
			CMakefileContent.Append(CMakeConfigFilesList);
			CMakefileContent.Append(IncludeDirectoriesList);
			CMakefileContent.Append(PreprocessorDefinitionsList);

			string CMakeProjectCmdArg = "";

			foreach (var Project in GeneratedProjectFiles)
			{
				foreach (var TargetFile in Project.ProjectTargets)
				{
					if (TargetFile.TargetFilePath == null)
					{
						continue;
					}

					var TargetName = TargetFile.TargetFilePath.GetFileNameWithoutAnyExtensions();		// Remove both ".cs" and ".

					foreach (UnrealTargetConfiguration CurConfiguration in Enum.GetValues(typeof(UnrealTargetConfiguration)))
					{
						if (CurConfiguration != UnrealTargetConfiguration.Unknown && CurConfiguration != UnrealTargetConfiguration.Development)
						{
							if (UnrealBuildTool.IsValidConfiguration(CurConfiguration))
							{
								if (TargetName == GameProjectName || TargetName == (GameProjectName + "Editor"))
								{
									CMakeProjectCmdArg = "-project=\"${GAME_PROJECT_FILE}\"";
								}

								string confName = Enum.GetName(typeof(UnrealTargetConfiguration), CurConfiguration);
								CMakefileContent.Append($"add_custom_target({TargetName}-{HostArchitecture}-{confName} ${{BUILD}} {TargetName} {HostArchitecture} {confName} {CMakeProjectCmdArg} $(ARGS))\n");
							}
						}
					}

					if (TargetName == GameProjectName || TargetName == (GameProjectName + "Editor"))
					{
						CMakeProjectCmdArg = "-project=\"${GAME_PROJECT_FILE}\"";
					}

					if (!String.IsNullOrEmpty(HostArchitecture))
					{
						CMakefileContent.Append($"add_custom_target({TargetName} ${{BUILD}} {TargetName} {HostArchitecture} Development {CMakeProjectCmdArg} $(ARGS) SOURCES ${{SOURCE_FILES}} ${{HEADER_FILES}} ${{CONFIG_FILES}})\n\n");
					}
				}
			}

			// Append a dummy executable target
            CMakefileContent.AppendLine("add_executable(FakeTarget ${SOURCE_FILES})");

			var FullFileName = Path.Combine(MasterProjectPath.FullName, ProjectFileName);

			bool writeSuccess = WriteFileIfChanged(FullFileName, CMakefileContent.ToString());
			return writeSuccess;
		}

		private static bool IsPathExcludedOnPlatform(string SourceFileRelativeToRoot, UnrealTargetPlatform targetPlatform)
		{
			switch (targetPlatform)
			{
				case UnrealTargetPlatform.Linux:
				{
					return IsPathExcludedOnLinux(SourceFileRelativeToRoot);
				}
				case UnrealTargetPlatform.Mac:
				{
					return IsPathExcludedOnMac(SourceFileRelativeToRoot);
				}
				case UnrealTargetPlatform.Win64:
				{
					return IsPathExcludedOnWindows(SourceFileRelativeToRoot);
				}
				default:
				{
					return false;
				}
			}
		}

		private static bool IsPathExcludedOnLinux(string SourceFileRelativeToRoot)
		{
			// minimal filtering as it is helpful to be able to look up symbols from other platforms
			return SourceFileRelativeToRoot.Contains("Source/ThirdParty/");
		}

		private static bool IsPathExcludedOnMac(string SourceFileRelativeToRoot)
		{
			return SourceFileRelativeToRoot.Contains("Source/ThirdParty/") ||
				SourceFileRelativeToRoot.Contains("/Windows/") ||
				SourceFileRelativeToRoot.Contains("/Linux/") ||
				SourceFileRelativeToRoot.Contains("/VisualStudioSourceCodeAccess/") ||
				SourceFileRelativeToRoot.Contains("/WmfMedia/") ||
				SourceFileRelativeToRoot.Contains("/WindowsDeviceProfileSelector/") ||
				SourceFileRelativeToRoot.Contains("/WindowsMoviePlayer/") ||
				SourceFileRelativeToRoot.Contains("/WinRT/");
		}

		private static bool IsPathExcludedOnWindows(string SourceFileRelativeToRoot)
		{
			// minimal filtering as it is helpful to be able to look up symbols from other platforms
			return SourceFileRelativeToRoot.Contains("Source/ThirdParty/");
		}

		/// Adds the include directory to the list, after converting it to relative to UE4 root
		private static string GetIncludeDirectory(string IncludeDir, string ProjectDir)
		{
			string FullProjectPath = Path.GetFullPath(MasterProjectPath.FullName);
			string FullPath = "";
			if (IncludeDir.StartsWith("/") && !IncludeDir.StartsWith(FullProjectPath))
			{
				// Full path to a fulder outside of project
				FullPath = IncludeDir;
			}
			else
			{
				FullPath = Path.GetFullPath(Path.Combine(ProjectDir, IncludeDir));
				FullPath = Utils.MakePathRelativeTo(FullPath, FullProjectPath);
				FullPath = FullPath.TrimEnd('/');
			}
			return FullPath;
		}

		#region ProjectFileGenerator implementation

		protected override bool WriteProjectFiles()
		{
			return WriteCMakeLists();
		}

		public override MasterProjectFolder AllocateMasterProjectFolder(ProjectFileGenerator InitOwnerProjectFileGenerator, string InitFolderName)
		{
			return new CMakefileFolder(InitOwnerProjectFileGenerator, InitFolderName);
		}

		/// <summary>
		/// Allocates a generator-specific project file object
		/// </summary>
		/// <param name="InitFilePath">Path to the project file</param>
		/// <returns>The newly allocated project file object</returns>
		protected override ProjectFile AllocateProjectFile(FileReference InitFilePath)
		{
			return new CMakefileProjectFile(InitFilePath);
		}

		public override void CleanProjectFiles(DirectoryReference InMasterProjectDirectory, string InMasterProjectName, DirectoryReference InIntermediateProjectFilesDirectory)
		{
			FileReference MasterProjectFile = FileReference.Combine(InMasterProjectDirectory, "CMakeLists.txt");
			if (MasterProjectFile.Exists())
			{
				MasterProjectFile.Delete();
			}
		}

		#endregion ProjectFileGenerator implementation
	}
}
