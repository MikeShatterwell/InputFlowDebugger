// Copyright Mike Desrosiers, All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

public class InputFlowDebugger : ModuleRules
{
	public InputFlowDebugger(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		// Add engine modules that are statically linked with this module
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"GameplayTags",
				"UMG", 
				"FieldNotification"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"ToolMenus",
					"WorkspaceMenuStructure",
					"UMGEditor",
					"EditorInteractiveToolsFramework",
					"SlateReflector"
				}
			);
		}
		
		// -------------------------------------------------------------------------
		// Selective Inclusion Logic
		// -------------------------------------------------------------------------
		
		ILogger Logger = Target.Logger;

		// Check the Project file to see which plugins are enabled in the current project
		if (Target.ProjectFile != null)
		{
			Logger.LogDebug("Project file found: {File}", Target.ProjectFile.FullName);
			
			// Read the .uproject file directly
			JsonObject RawObject = JsonObject.Read(Target.ProjectFile);
			
			JsonObject[] PluginList;
			if (RawObject.TryGetObjectArrayField("Plugins", out PluginList))
			{
				foreach (JsonObject ReferenceObject in PluginList)
				{
					string PluginName;
					if (!ReferenceObject.TryGetStringField("Name", out PluginName)) continue;

					bool IsPluginEnabled = false;
					if (!ReferenceObject.TryGetBoolField("Enabled", out IsPluginEnabled)) IsPluginEnabled = false;

					Logger.LogDebug(
						"Found Plugin named {PluginName}, Enabled: {Enabled}",
						PluginName,
						IsPluginEnabled ? "Yes" : "No"
					);

					if (IsPluginEnabled)
					{
						if (PluginName == "EnhancedInput")
						{
							PublicDependencyModuleNames.Add("EnhancedInput");
						}
						else if (PluginName == "CommonUI")
						{
							PrivateDependencyModuleNames.AddRange(
								new string[]
								{
									"CommonUI",
									"CommonInput", 
								}
							);
						}
						else if (PluginName == "ModelViewViewModel")
						{
							PrivateDependencyModuleNames.Add("ModelViewViewModel");
						}
					}

					var Symbol = $"WITH_PLUGIN_{PluginName.ToUpper()}={(IsPluginEnabled ? 1 : 0)}";
					PublicDefinitions.Add(Symbol);

					Logger.LogDebug("Adding symbol definition {SymbolDef}", Symbol);
				}
			}
		}
		else
		{
			Logger.LogDebug("No Project file found (Target.ProjectFile is null). Skipping selective plugin inclusion.");
		}
	}
}