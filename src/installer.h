#pragma once

namespace D2RL { struct PluginContext; }

enum class InstallResult { AlreadyInstalled, Installed, NoLooseData, Failed, RecipesRemoved };
InstallResult RunFirstRunInstall(const D2RL::PluginContext& context) noexcept;
