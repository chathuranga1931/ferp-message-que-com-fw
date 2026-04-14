/// AGI-Drive Modules
import { Service } from '../_base';
import { Fetch } from '../../utils/fetch';
import { Emitter } from '../../utils/emitter';
import { Heartbeat } from '../../utils/heartbeat';

/**************
 *  TYPEDEFS  *
 **************/

/** Status Typings. */
export namespace Status_t {
    /****************
     *  DATA TYPES  *
     ****************/

    /** JSON Status Object */
    export interface IData {
        ssid: string;
        password: string;
    }

    /******************
     *  ENUMERATIONS  *
     ******************/

    /** Current Connection State. */
    export enum State {
        CONNECTED,
        DISCONNECTED,
    }

    /************
     *  EVENTS  *
     ************/

    /** Available Status Service Events. */
    export interface Events {
        status: [IData];
    }
}

/********************
 *  IMPLEMENTATION  *
 ********************/

/** Status Service Implementation. */
class _Status_impl extends Emitter.Typed<Status_t.Events> implements Service.From<'init'> {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Status Heartbeat. */
    private m_hb = new Heartbeat(this.m_onHeartbeat.bind(this), 1000);

    /** Current Connection State. */
    private m_state: Status_t.State = Status_t.State.DISCONNECTED;

    /***********************
     *  GETTERS / SETTERS  *
     ***********************/

    /** Gets the current state. */
    get state() {
        return this.m_state;
    }

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Initialises the service. */
    async init() {
        this.m_hb.start(); // begin heartbeat for the current server status
    }

    /** Coordinates fetching the status data. */
    async fetch() {
        return Fetch.request('user.data').then((res) => res.maybe());
    }

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /**
     * Handles updating and triggering a state change event.
     * @param next                          Next available state.
     */
    private m_updateState(next: Status_t.State) {
        // if the state is already the same, do nothing
        if (next === this.m_state) return;

        // otherwise latch and emit the change
        this.m_state = next;
    }

    /********************
     *  EVENT HANDLERS  *
     ********************/

    /** Coordinates an asynchronous heartbeat. */
    private async m_onHeartbeat() {
        // get the current status
        const result = await this.fetch();

        // declare connection state changes based on the result
        this.m_updateState(Status_t.State[result.is('none') ? 'DISCONNECTED' : 'CONNECTED']);

        // unwrap the result into a suitable state value
        const status = result.unwrap({ ssid: '', password: '' });

        // emit the current status
        this.trigger('status', status);
    }
}

/// Singleton Instance.
export const Status = new _Status_impl();
