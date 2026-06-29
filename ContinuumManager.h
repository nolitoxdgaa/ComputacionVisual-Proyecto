#pragma once
#include <string>

enum class ContinuumState {
    REAL_ENVIRONMENT = 0,
    AUGMENTED_REALITY = 1,
    AUGMENTED_VIRTUALITY = 2,
    VIRTUAL_REALITY = 3
};

class ContinuumManager {
public:
    ContinuumManager() : currentState(ContinuumState::REAL_ENVIRONMENT) {}

    void setState(ContinuumState state) {
        currentState = state;
    }

    ContinuumState getState() const {
        return currentState;
    }

    std::string getStateName() const {
        switch (currentState) {
            case ContinuumState::REAL_ENVIRONMENT: return "Entorno Real (1)";
            case ContinuumState::AUGMENTED_REALITY: return "Realidad Aumentada (2)";
            case ContinuumState::AUGMENTED_VIRTUALITY: return "Virtualidad Aumentada (3)";
            case ContinuumState::VIRTUAL_REALITY: return "Realidad Virtual (4)";
        }
        return "Desconocido";
    }

    void handleKeyPress(int key) {
        // Handle keyboard switching (keys '1' to '4')
        if (key == '1') currentState = ContinuumState::REAL_ENVIRONMENT;
        else if (key == '2') currentState = ContinuumState::AUGMENTED_REALITY;
        else if (key == '3') currentState = ContinuumState::AUGMENTED_VIRTUALITY;
        else if (key == '4') currentState = ContinuumState::VIRTUAL_REALITY;
    }

private:
    ContinuumState currentState;
};
