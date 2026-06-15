#pragma once

#include <QKeyEvent>
#include <Qt>

#include <glm/glm.hpp>

#include <QSet>

class ViewportInputState
{
public:
    bool handleKeyPress(const QKeyEvent* event)
    {
        if (event->isAutoRepeat()) {
            return isMovementKey(event->key());
        }
        if (!isMovementKey(event->key())) {
            return false;
        }
        m_pressedKeys.insert(event->key());
        if (event->key() == Qt::Key_Shift) {
            m_boostHeld = true;
        }
        return true;
    }

    bool handleKeyRelease(const QKeyEvent* event)
    {
        if (event->isAutoRepeat()) {
            return isMovementKey(event->key());
        }
        if (!isMovementKey(event->key())) {
            return false;
        }
        m_pressedKeys.remove(event->key());
        if (event->key() == Qt::Key_Shift) {
            m_boostHeld = false;
        }
        return true;
    }

    void clear()
    {
        m_pressedKeys.clear();
        m_boostHeld = false;
    }

    glm::vec3 linearInput() const
    {
        glm::vec3 input{0.0f};
        if (m_pressedKeys.contains(Qt::Key_D)) {
            input.x += 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_A)) {
            input.x -= 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_E)) {
            input.y += 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_Q)) {
            input.y -= 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_W)) {
            input.z += 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_S)) {
            input.z -= 1.0f;
        }
        return input;
    }

    glm::vec3 angularInput() const
    {
        glm::vec3 input{0.0f};
        if (m_pressedKeys.contains(Qt::Key_Right)) {
            input.y += 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_Left)) {
            input.y -= 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_Up)) {
            input.x -= 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_Down)) {
            input.x += 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_X)) {
            input.z += 1.0f;
        }
        if (m_pressedKeys.contains(Qt::Key_Z)) {
            input.z -= 1.0f;
        }
        return input;
    }

    bool boostHeld() const { return m_boostHeld; }

    bool hasMovementInput() const
    {
        return !m_pressedKeys.isEmpty();
    }

private:
    static bool isMovementKey(int key)
    {
        switch (key) {
        case Qt::Key_W:
        case Qt::Key_A:
        case Qt::Key_S:
        case Qt::Key_D:
        case Qt::Key_Q:
        case Qt::Key_E:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Z:
        case Qt::Key_X:
        case Qt::Key_Shift:
            return true;
        default:
            return false;
        }
    }

    QSet<int> m_pressedKeys;
    bool m_boostHeld = false;
};
