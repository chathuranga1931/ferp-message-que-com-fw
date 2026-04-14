/** Heartbeat Implementation. */
export class Heartbeat {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Denotes if currently running. */
    private m_running = false;

    /***********************
     *  GETTERS / SETTERS  *
     ***********************/

    /** Returns if currently running. */
    get running() {
        return this.m_running;
    }

    /*****************
     *  CONSTRUCTOR  *
     *****************/

    /**
     * Constructs a heart beat instance with a base callback.
     * @param m_next                            Callback instance.
     * @param rate                              Refresh rate.
     */
    constructor(private readonly m_next: Utils_t.Functor.Any<Promise<any>>, public readonly rate: number) {}

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Starts the heartbeat instance. */
    start() {
        if (this.m_running) return; // stop if already running
        this.m_running = true; // set into run-mode
        this.m_loop(); // and begin cycling
    }

    /** Forces a single execution. */
    force() {
        this.m_loop(true);
    }

    /** Stops the heartbeat instance. */
    stop() {
        this.m_running = false;
    }

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /**
     * Coordinates an asynchronous delay.
     * @param duration                  Duration to wait.
     */
    private m_sleep(duration: number) {
        return new Promise<void>((resolve) => setTimeout(resolve, duration));
    }

    /** Coordinates the asynchronous heartbeat loop. */
    private async m_loop(forceful: boolean = false) {
        await this.m_next(); // coordinate the next callback
        await this.m_sleep(this.rate); // throttle the loop

        // and run again if still able to do so
        if (!forceful && this.running) this.m_loop();
    }
}
