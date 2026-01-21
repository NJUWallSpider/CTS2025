# Solution Scorer

The `scorer.py` script evaluates the quality of a generated crew roster (`rosterResult.csv`) against the competition's objectives and constraints.

## Metrics

The total score is calculated based on the following components:

1.  **Average Daily Flight Hours (Maximize)**:
    *   Formula: `(Total Flight Hours / Total Duty Days) * 100`
    *   Higher is better.
2.  **Uncovered Flights (Penalty)**:
    *   `-5` points per unassigned flight.
3.  **Overnight Stays (Penalty)**:
    *   `-0.5` points per day spent at a non-base airport (Layover).
4.  **Positioning/Deadheading (Penalty)**:
    *   `-0.5` points per positioning task (flying as a passenger or taking a bus to reposition).
5.  **Violations (Penalty)**:
    *   `-10` points per rule violation.
    *   Checked rules include:
        *   Task overlaps.
        *   Connection times (Min/Max).
        *   Duty limits (Flight time, Duty length, Task count).
        *   Rest requirements (12h min rest, 4-on-2-off pattern).
        *   Qualifications.
        *   Location consistency.

## Usage

**Important**: The file paths are currently hardcoded in the script. You must edit them before running.

1.  Open `scorer.py`.
2.  Scroll to the bottom (`if __name__ == '__main__':` block).
3.  Update the `data_path` and `submission_file` variables:

    ```python
    if __name__ == '__main__':
        # Update these paths
        data_path = './data/0623/'  # Path to the data directory
        submission_file = './exact_solver/report/0623/rosterResult.csv' # Path to your result
        calculate_score(data_path, submission_file)
    ```

4.  Run the script:

    ```bash
    python3 scorer/scorer.py
    ```

## Output

The script prints a single line summary to stdout:

```text
总得分：850.50 值勤日日均飞时：4.20 未覆盖航班数：0 ...
```
