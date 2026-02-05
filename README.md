# 🌍 C++ Civilization Simulation

Welcome to **Civilization Simulation**, a C++ project using **SFML** for graphics and game simulation!

## 🚀 Features

- 🗺️ Interactive map simulation
- ⚡ Optimized rendering with SFML
- 🛠️ Built using CMake and Visual Studio
- 🔍 Real-time data visualization

## 📷 Screenshots

## Important Notes and Controls

**F1. Hide/Show the GUI (ImGui overlay).**
3. Zoom feature! Scroll to zoom in and click and drag to move around the map, press 3 again to go to default zoom.
4. Toggle warmonger highlights (also available in the ImGui Tools window).
5. Toggle the news feed (ImGui window).
6. Toggle war highlights (also available in the ImGui Tools window).
8. Trigger a plague manually (also available in the ImGui Tools window).
9. Toggle add-country mode. Click on a land pixel to add a country.
**L. Wealth leaderboard (ImGui table).**
**E. Technology editor for selected country (ImGui window).**
**M. Country template editor (ImGui window used by add-country mode).**
**G. Toggle 2D/Globe view.**
**D. DEBUG MODE TOGGLE - Shows/hides technology and civic unlock messages.**
**T. TURBO MODE - 10 YEARS PER SECOND with nuclear performance optimization!**
**F. FAST FORWARD MODE - Simulates 100 years in 2 seconds with optimized simulation!**
**Z. MEGA TIME JUMP - Simulate THOUSANDS of years! (Legacy full-screen UI is unchanged.)**

## 🚀 Nuclear Performance Optimization Features

### TURBO MODE (T key) - 10 YEARS/SECOND
- **Event-driven simulation** - no wasted CPU cycles
- **Smart rendering** - only draws when state changes
- **Parallel processing** - multi-threaded country updates
- **Intelligent frame limiting** - 30 FPS during turbo mode
- **Real-time performance monitoring** - tracks simulation timing
- **All normal game mechanics preserved**

### Fast Forward Mode (F key) - 100 YEARS/2 SECONDS  
- **Accelerated population growth** (5x normal rate)
- **Rapid territorial expansion** (countries expand every 5 years instead of every year)
- **Simplified but realistic wars** (reduced frequency but still strategic)
- **Quick city founding** (every 20 years with large populations)
- **Plague events** (can still occur during fast forward)
- **Technology & culture advancement** (100 years worth of research in seconds)
- **All changes are persistent** - the world continues from the fast-forwarded state

### MEGA TIME JUMP (Z key) - THOUSANDS OF YEARS!
- **🌍 EPIC HISTORICAL SIMULATION** - Jump from 4900 BCE to 2025 CE (7,000+ years!)
- **⚡ LIGHTNING FAST** - Simulates ~1000 years per second
- **📊 LIVE ETA TRACKING** - Real-time progress and time estimates
- **🏛️ CIVILIZATION EVOLUTION** - Countries rise, fall, and transform
- **⚔️ MEGA WARS** - Epic conflicts reshape the world every 25 years
- **🦠 DEVASTATING PLAGUES** - More frequent and deadly pandemics
- **🧠 TECH ACCELERATION** - 3x faster research and advancement
- **💀 EXTINCTION EVENTS** - Witness civilizations disappear forever
- **🌟 HISTORICAL TRACKING** - See major events, wars, and superpowers
- **🗺️ DRAMATIC MAP CHANGES** - The world transforms completely!

## Documentaiton 

Warmongers can expand to 10x the country size limit
    else if (m_type == Type::Warmonger) {
        return static_cast<int>(baseLimit * 10);
Trader countries are at the limit
Pacifists are at 10% the limit but have a chance to be at 50% of the limit
