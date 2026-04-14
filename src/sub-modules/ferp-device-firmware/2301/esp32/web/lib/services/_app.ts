/// AGI-Drive Modules
import { Router } from './router';
import { Service } from './_base';

/// AGI-Drive Services
import { Navbar } from './navbar';
import { Status } from './status';
import { Factory } from '../components/_factory';
import { Modal } from '../components/modal';

/** AGI-Drive Application. */
class _App_impl {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Available Initable Services. */
    readonly services: Service.From<'init'>[] = [Router, Factory, Status, Navbar, Modal];

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Starts the application handling. */
    async start() {
        await this.m_preload();
        await this.m_handover();
    }

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /** Coordinates preloading any services available. */
    private async m_preload() {
        // reduce all the available services into a chain of sequential resolution
        return this.services.reduce((p, next) => p.then(() => next.init()), Promise.resolve());
    }

    /** Begins page-routing delegation. */
    private async m_handover() {
        return Router.reload(); // request a reload of the current page
    }
}

/// Singleton Instance.
export const App = new _App_impl();
