#include "news.h"

News::News() : m_showWindow(false) {
    m_background.setFillColor(sf::Color(0, 0, 0, 175)); // Semi-transparent black
    m_background.setSize(sf::Vector2f(300, 400));      // Adjust size as needed
    m_background.setPosition(10, 10);                  // Adjust position as needed

    m_newsText.setCharacterSize(16);
    m_newsText.setFillColor(sf::Color::White);
}

void News::addEvent(const std::string& event) {
    m_events.push_back(event);
    if (m_events.size() > 10) {
        m_events.erase(m_events.begin());
    }
}

void News::toggleWindow() {
    m_showWindow = !m_showWindow;
}

void News::setWindowVisible(bool visible) {
    m_showWindow = visible;
}

void News::clearEvents() {
    m_events.clear();
}

void News::render(sf::RenderWindow& window, const sf::Font& font) {
    if (!m_showWindow) return;

    m_newsText.setFont(font);
    window.draw(m_background);

    std::string newsString;
    for (const auto& event : m_events) {
        newsString += event + "\n";
    }

    m_newsText.setString(newsString);
    m_newsText.setPosition(20, 20); // Adjust position as needed
    window.draw(m_newsText);
}

bool News::isWindowVisible() const {
    return m_showWindow;
}
