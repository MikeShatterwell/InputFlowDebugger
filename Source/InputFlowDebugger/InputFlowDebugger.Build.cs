// Copyright Mike Desrosiers, All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;
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
				"CoreUObject",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
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
					"SlateReflector",
				}
			);
		}

		// -------------------------------------------------------------------------
		// Selective Inclusion Logic
		// -------------------------------------------------------------------------
		ILogger Logger = Target.Logger;

		// Build a lookup of every plugin listed in the .uproject and whether
		// it is enabled. If there is no project file, this stays empty and every
		// optional plugin below is treated as disabled (macro = 0).
		Dictionary<string, bool> EnabledPlugins = new Dictionary<string, bool>(System.StringComparer.OrdinalIgnoreCase);

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

					bool IsPluginEnabled;
					if (!ReferenceObject.TryGetBoolField("Enabled", out IsPluginEnabled)) IsPluginEnabled = false;

					EnabledPlugins[PluginName] = IsPluginEnabled;

					Logger.LogDebug(
						"Found Plugin named {PluginName}, Enabled: {Enabled}",
						PluginName,
						IsPluginEnabled ? "Yes" : "No"
					);
				}
			}
		}
		else
		{
			Logger.LogDebug("No Project file found (Target.ProjectFile is null). Treating all optional plugins as disabled.");
		}

		// Local helper: is a plugin both listed AND enabled in the .uproject?
		bool IsPluginEnabledInProject(string PluginName)
		{
			return EnabledPlugins.TryGetValue(PluginName, out bool bEnabled) && bEnabled;
		}

		// Tracks which WITH_PLUGIN_* symbols we've already emitted so we never
		// define the same macro twice (which would trigger a redefinition warning).
		HashSet<string> DefinedPluginSymbols =
			new HashSet<string>(System.StringComparer.OrdinalIgnoreCase);

		// Local helper: always defines WITH_PLUGIN_<NAME> to 1 or 0.
		void DefinePluginSymbol(string PluginName, bool bAvailable)
		{
			if (!DefinedPluginSymbols.Add(PluginName))
			{
				return; // already defined
			}

			string Symbol = $"WITH_PLUGIN_{PluginName.ToUpper()}={(bAvailable ? 1 : 0)}";
			PublicDefinitions.Add(Symbol);
			Logger.LogDebug("Adding symbol definition {SymbolDef}", Symbol);
		}

		// -------------------------------------------------------------------------
		// Every plugin this module has #if-guarded C++ for MUST be listed here so
		// its macro is always defined (to 0 when the plugin is missing/disabled),
		// regardless of whether the plugin appears in the .uproject at all.
		// -------------------------------------------------------------------------
		string[] OptionalPlugins =
		{
			"EnhancedInput",
			"CommonUI",
			"ModelViewViewModel",
			"UMGWidgetPreview",
		};

		foreach (string PluginName in OptionalPlugins)
		{
			bool bEnabled = IsPluginEnabledInProject(PluginName);

			// bAvailable reflects whether the module is actually linked
			bool bAvailable = bEnabled;

			if (bEnabled)
			{
				switch (PluginName)
				{
					case "EnhancedInput":
						PublicDependencyModuleNames.Add("EnhancedInput");
						break;

					case "CommonUI":
						PrivateDependencyModuleNames.AddRange(
							new string[]
							{
								"CommonUI",
								"CommonInput",
							}
						);
						break;

					case "ModelViewViewModel":
						PrivateDependencyModuleNames.Add("ModelViewViewModel");
						break;

					case "UMGWidgetPreview":
						if (Target.bBuildEditor)
						{
							PrivateDependencyModuleNames.Add("UMGWidgetPreview");
						}
						else
						{
							bAvailable = false;
						}
						break;
				}
			}

			DefinePluginSymbol(PluginName, bAvailable);
		}
	}
}