#pragma once

#include "../Loader/LoadData.hpp"
#include "./SolutionState.hpp"
#include <vector>
#include <map>
#include <string>

/**
 * @brief Internal helper struct for tracking detailed dynamic state of crew during evaluation
 * 
 * This struct maintains runtime state information for each crew member during schedule evaluation.
 * It tracks both duty-level metrics (flight tasks, total tasks, duty time) and cycle-level metrics
 * (total duties, total hours) to ensure all scheduling constraints are met.
 * 
 * The state is updated as tasks are assigned/removed and helps validate scheduling rules like:
 * - Maximum flight tasks per duty
 * - Maximum total tasks per duty  
 * - Maximum duty time limits
 * - Required rest periods between duties
 * - Maximum cycle duration and total cycle hours
 */


class SolutionConstructor {
public:
    /**
     * @brief Constructor
     * @param data All of the data loaded
     */
    explicit SolutionConstructor(const DataLoader& data);

    // Generates a new, complete schedule.
    SolutionState generate_schedule();

private:
    const DataLoader& data_; // Holds a const reference to the original data
};