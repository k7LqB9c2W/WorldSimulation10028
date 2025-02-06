# 🌍 C++ Civilization Simulation

Welcome to **Civilization Simulation**, a C++ project using **SFML** for graphics and game simulation!

## 🚀 Features

- 🗺️ Interactive map simulation
- ⚡ Optimized rendering with SFML
- 🛠️ Built using CMake and Visual Studio
- 🔍 Real-time data visualization

## 📷 Screenshots

## Important Notes and Controls

3. Zoom feature! Scroll to zoom in and click and drag to move around the map, press 3 again to go to default zoom.
4. Shows all warmonger countries.
5. Shows the news feed (shows wars, when great plagues happen, the founding of cities).
6. Shows all active wars.

'9.' Enables add country mode. Click on a pixel to add a country.

## Documentaiton 

Warmongers can expand to 10x the country size limit
    else if (m_type == Type::Warmonger) {
        return static_cast<int>(baseLimit * 10);
Trader countries are at the limit
Pacifists are at 10% the limit but have a chance to be at 50% of the limit
