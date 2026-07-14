/**
 * @file UCI.h
 * @brief Universal Chess Interface (UCI) protocol declarations.
 * 
 * Declares the main command loop to interact with chess GUIs.
 */

#pragma once
#include "Board.h"

namespace UCI {
    /**
     * @brief Launches the UCI loop.
     * Listens to standard input, parses UCI commands, updates the board,
     * and triggers search.
     */
    void loop();
}

// Print confirmation message at the end of the file.
// Confirmation: UCI.h created successfully.
