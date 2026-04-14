/// AGI-Drive Modules
import { Service } from '../_base';
import { Status, Status_t } from '../status';

/** Navigation Bar Service. */
class _Navbar_impl implements Service.From<'init'> {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Timestamp Element. */
    readonly $timestamp = $('#nav-timestamp');

    /** Device Name Element. */
    readonly $device = $('#nav-status');

    /***********************
     *  GETTERS / SETTERS  *
     ***********************/

    /** Coordinates setting the navigation bars device text. */
    set device(next: string) {
        this.$device.text(next); // set the next required value
    }

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Initialises the service events. */
    async init() {
        this.m_update(); // begin update handling
        this.m_registerListeners(); // registers all required listeners
    }

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /** Coordinates routinely updating the current time display. */
    private m_update() {
        const current = new Date(); // get the current date
        const date = current.toLocaleDateString();
        const time = current.toLocaleTimeString();

        // set for display
        this.$timestamp.html(`<span class="d-none d-md-inline">${date}, </span>${time}`);

        // and always request once the next animation frame comes
        requestAnimationFrame(() => this.m_update());
    }

    /********************
     *  EVENT HANDLERS  *
     ********************/

    /** Register all Navbar listeners. */
    private m_registerListeners() {
        // handle device name updates
        Status.listen('status', this.m_onStatusUpdate.bind(this));
    }

    /** Coordinates updating the current device text. */
    private m_onStatusUpdate() {
        // determine if currently connected
        const connected = Status.state === Status_t.State.CONNECTED;
        const context = connected ? 'success' : 'danger';
        const text = connected ? 'Connected' : 'Disconnected';

        // finally update the device state
        this.$device.removeClass('badge-danger badge-success').addClass(`badge-${context}`).text(text);
    }
}

/// Singleton Instance
export const Navbar = new _Navbar_impl();
