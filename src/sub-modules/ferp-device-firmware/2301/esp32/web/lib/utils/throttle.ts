/**
 * Wraps a given function in a throttler callback.
 * @param delay                             Delay to throttle.
 * @param callback                          Callback to call.
 */
export const throttle = (delay: number, callback: Function) => {
    let waiting = false; // denote as initially NOT waiting
    let queued: any[] | undefined = undefined; // queued arguments

    // internal throttler
    const m_internal = () => {
        // if nothing is there, denote as complete
        if (queued === undefined) return (waiting = false);

        // otherwise recall the callback
        callback(...queued);
        queued = undefined;
        setTimeout(m_internal, delay);
    };

    // wrap the callback as needed
    return function () {
        // if still waiting, then queue the final arguments
        if (waiting) return (queued = [...arguments]);

        // otherwise call the throttler
        callback(...arguments);
        waiting = true;
        setTimeout(m_internal, delay);
    };
};
