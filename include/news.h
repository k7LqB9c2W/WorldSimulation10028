#pragma once

#include <SFML/Graphics.hpp>
#include <vector>

class News {
public:
    News();

    void addEvent(const std::string& event);
    void toggleWindow();
    void setWindowVisible(bool visible);
    void clearEvents();
    const std::vector<std::string>& getEvents() const { return m_events; }
    void render(sf::RenderWindow& window, const sf::Font& font);
    bool isWindowVisible() const;

private:
    std::vector<std::string> m_events;
    bool m_showWindow;
    sf::RectangleShape m_background;
    sf::Text m_newsText;
};
