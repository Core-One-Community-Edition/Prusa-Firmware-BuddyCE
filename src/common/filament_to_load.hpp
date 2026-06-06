#pragma once

#include <filament.hpp>
#include <color.hpp>

// TODO: Remove this ugly interface
namespace filament {

FilamentType get_type_to_load();
void set_type_to_load(FilamentType filament);

std::optional<Color> get_color_to_load();
void set_color_to_load(std::optional<Color> color);

/// Filament type the user last selected in the standalone preheat menu (cleared by Cooldown)
FilamentType get_preheated_type();
void set_preheated_type(FilamentType filament);

} // namespace filament
