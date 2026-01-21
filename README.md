# CTS2025 Crew Rostering System

This repository contains the solution for the CTS2025 Crew Rostering problem. The system is designed to assign flight crews to flight schedules while adhering to complex aviation regulations, qualification requirements, and operational constraints.

## Project Architecture

The solution adopts a two-stage hybrid approach to balance solution quality and computational efficiency:

1.  **Heuristic Solver (`heuristic/`)**:
    *   **Goal**: Quickly generate a high-quality initial feasible solution.
    *   **Method**: Uses a constructive greedy algorithm followed by a Parallel Simulated Annealing (SA) algorithm with a Ruin & Recreate (Large Neighborhood Search) strategy.
    *   **Output**: Produces a valid roster (`rosterResult.csv`) used as a warm start for the exact solver.

2.  **Exact Solver (`exact_solver/`)**:
    *   **Goal**: Optimize the roster to minimize costs and violations.
    *   **Method**: Implements a **Column Generation** framework using **Gurobi**.
        *   **Master Problem**: Set Partitioning Problem to select the best combination of legal crew rosters.
        *   **Subproblem**: Shortest Path Problem with Resource Constraints (SPPRC) to generate valid pairings/rosters for individual crew members.
    *   **Input**: Takes the solution from the Heuristic solver to initialize the column pool.

3.  **Scorer (`scorer/`)**:
    *   **Goal**: Evaluate the solution against the competition metrics.
    *   **Metrics**: Flight hours utilization, unassigned flights, overnight stays, positioning (deadheading), and rule violations.

## Directory Structure

```text
.
├── data/                  # Input CSV files (Flight, Crew, Rules, etc.)
├── exact_solver/          # C++23 Column Generation Solver (requires Gurobi)
├── heuristic/             # C++20 Heuristic Solver (LNS/Simulated Annealing)
├── scorer/                # Python evaluation script
└── 运行指导.md            # Original operational guide (Chinese)
```

## Prerequisites

*   **OS**: Linux or macOS (Recommended).
*   **C++ Compiler**: GCC or Clang supporting **C++20** (for Heuristic) and **C++23** (for Exact Solver).
*   **CMake**: Version 3.10 or higher.
*   **Gurobi Optimizer**: Required for `exact_solver`. Ensure the `GUROBI_HOME` environment variable is set.
*   **Python 3**: For `scorer` and data analysis scripts.
    *   Dependencies: `pandas`

## Quick Start Guide

### 1. Build and Run the Heuristic Solver

This step generates the initial solution.

```bash
cd heuristic
mkdir build && cd build
cmake ..
make -j4
./Heuristic
```
*Output*: `heuristic/report/<version>/rosterResult.csv`

### 2. Build and Run the Exact Solver

This step refines the solution using mathematical optimization.

**Note**: Ensure Gurobi is installed and configured.

```bash
cd exact_solver
mkdir build && cd build
cmake ..
make -j4
./ExactSolver
```
*Output*: `exact_solver/report/<version>/rosterResult.csv`

### 3. Evaluate the Solution

Run the python scorer to verify the quality of your result.

```bash
python3 scorer/scorer.py
```
*(Note: You may need to adjust the input file path in `scorer.py` to point to the `exact_solver` output)*

## Configuration

*   **Data Version**: Controlled in `main.cpp` of both solvers (default: "0623").
*   **Parameters**:
    *   `heuristic`: Algorithm parameters (Temperature, Iterations) are in `src/Solver/Solver.cpp`.
    *   `exact_solver`: Gurobi parameters and constraints are in `src/main.cpp`.

## License

Private Repository for CTS2025.
