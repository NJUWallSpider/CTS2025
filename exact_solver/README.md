# Exact Solver (Column Generation)

The Exact Solver is the second stage of the CTS2025 pipeline. It uses mathematical programming to optimize the initial solution provided by the Heuristic Solver.

## Algorithm: Column Generation

The problem is modeled as a large-scale integer programming problem, solved using the **Column Generation** technique.

### 1. Master Problem (Set Partitioning)
*   **Formulation**: Modeled as a Set Partitioning/Covering problem.
*   **Objective**: Minimize total cost (unassigned flights, positioning costs, rule violations).
*   **Decision Variables**: Binary variables representing whether a specific *roster* (a sequence of flights/duties for a crew member) is selected.
*   **Constraints**:
    *   Each flight must be covered exactly once (or penalties are incurred).
    *   Each crew member can be assigned at most one roster.

### 2. Subproblem (SPPRC)
*   **Role**: Generates new valid rosters ("columns") with negative reduced costs to add to the Master Problem.
*   **Algorithm**: **Shortest Path Problem with Resource Constraints**.
*   **Graph**: Nodes represent flights/tasks; edges represent feasible connections.
*   **Resources**: Duty time, flight time, accumulated fatigue, etc.

### 3. Workflow
1.  **Warm Start**: Loads the initial `rosterResult.csv` from the Heuristic solver.
2.  **RMP (Restricted Master Problem)**: Solved using **Gurobi** LP relaxation.
3.  **Pricing**: Dual values from the RMP are used to update edge weights in the Subproblem.
4.  **Column Generation**: The Subproblem finds new rosters that improve the objective.
5.  **Branch-and-Bound / Integer Fixation**: Once column generation converges, integer constraints are enforced to obtain the final schedule.

## Build Instructions

### Requirements
*   **Gurobi Optimizer** (Licensed).
*   **C++23** compliant compiler.
*   CMake 3.10+.

### Environment Setup
You must set the `GUROBI_HOME` environment variable to your Gurobi installation path.

Example (add to `~/.zshrc` or `~/.bashrc`):
```bash
export GUROBI_HOME="/opt/gurobi1202/linux64"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:${GUROBI_HOME}/lib"
```

### Compilation

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Execution

```bash
./ExactSolver
```

## Configuration

*   **Data Path**: Set in `src/main.cpp` (`data_version`).
*   **Parameters**:
    *   `beam_width`: Beam search width for the subproblem.
    *   `MAX_ITERATIONS`: Maximum CG iterations.
    *   `NUM_THREADS`: Number of threads for parallel pricing.

## Troubleshooting

*   **"Gurobi not found"**: Ensure `GUROBI_HOME` is set correctly and `FindGUROBI.cmake` can locate your version.
*   **License Error**: Ensure `gurobi_cl --version` works in your terminal.
