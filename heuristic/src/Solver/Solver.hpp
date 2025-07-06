// src/Solver.h

#pragma once

#include "../Loader/LoadData.hpp"
// #include "../Problem/CrewScheduler.h"
// #include "../GA/GeneticAlgorithm.h"
// #include "../Parallel/ParallelManager.h"

/**
 * @class Solver
 * @brief The main driver class that orchestrates the entire solution process.
 * 

 */
class Solver {
public:
    Solver(int argc, char* argv[]);
    void run();

private:
    // ParallelManager parallel_manager_;
    // ... 其他成员
};