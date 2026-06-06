#include "filament_to_load.hpp"

static FilamentType filament_to_load = FilamentType::none;

FilamentType filament::get_type_to_load() {
    return filament_to_load;
}

void filament::set_type_to_load(FilamentType filament) {
    filament_to_load = filament;
}

static std::optional<Color> color_to_load { std::nullopt };

std::optional<Color> filament::get_color_to_load() {
    return color_to_load;
}

void filament::set_color_to_load(std::optional<Color> color) {
    color_to_load = color;
}

static FilamentType preheated_type = FilamentType::none;

FilamentType filament::get_preheated_type() {
    return preheated_type;
}

void filament::set_preheated_type(FilamentType filament) {
    preheated_type = filament;
}
