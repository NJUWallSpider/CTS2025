# Heuristic Solver

The Heuristic Solver is the first stage of the CTS2025 scheduling pipeline. It is responsible for creating a feasible initial roster, which serves as a warm start for the Exact Solver.

## Algorithm Overview

The solver employs a **Large Neighborhood Search (LNS)** framework driven by **Simulated Annealing**.

### Phase 1: Constructive Initialization
*   **Priority Scoring**: Crews are sorted based on a composite score derived from:
    *   Qualification flexibility (fewer qualifications -> higher priority).
    *   Base station strategic value.
*   **Greedy Assignment**: A constructive heuristic assigns flights to crews sequentially, ensuring all hard constraints (qualifications, rest periods, maximum duty times) are met.

### Phase 2: Parallel Ruin & Recreate (Optimization)
*   **Strategy**: This phase iteratively improves the solution.
*   **Ruin (Destruction)**: A subset of the roster is randomly removed ("ruined").
*   **Recreate (Repair)**: The removed flights are re-assigned to crews using a greedy approach, potentially finding better fits.
*   **Acceptance Criterion**: Uses **Simulated Annealing** to accept worse solutions with a probability that decreases over time (Temperature), helping to escape local optima.
*   **Concurrency**: The process runs in **parallel** (multi-threaded), exploring multiple search paths simultaneously to maximize coverage of the solution space.

## Build Instructions

### Requirements
*   **C++20** compliant compiler.
*   CMake 3.10+.

### Compilation

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Execution

```bash
./Heuristic
```

## Output

The solver outputs various reports in `report/<data_version>/`:
*   `rosterResult.csv`: The final schedule (Input for Exact Solver).
*   `schedule_report.txt`: Human-readable summary.
*   `assignment_report.csv`: Detailed assignment logs.
*   `crew_flight_consistency.txt`: Validation logs.

## Configuration

Key hyperparameters can be tuned in `src/Solver/Solver.cpp`:
*   `max_iterations`: Number of SA iterations (Default: 15000).
*   `num_paths`: Number of parallel search paths (Default: 16).
*   `initial_temperature`: Starting temperature for SA (Default: 700.0).
*   `initial_ruin_percentage`: Percentage of the schedule to destroy in each step (Default: 5%).
