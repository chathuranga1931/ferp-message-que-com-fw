/// AGI-Drive Modules
import { UID } from './uuid';

/** Browser Emitter Functionality. */
export namespace Emitter {
    /*************
     *  TYPDEFS  *
     *************/

    /** Base Emitter Functor. */
    export type Functor = Utils_t.Functor.Any<void>;

    /** Bindable Functor. */
    export type Bindable<A extends any[]> = (...args: A) => void;

    /** Mapped Emitter Arguments. */
    export type Map<T extends object> = Record<keyof T, any[]>;

    /*************
     *  OBJECTS  *
     *************/

    /** Typed-Emitter Implementation. */
    export class Typed<T extends Map<T>> extends UID {
        /****************
         *  PROPERTIES  *
         ****************/

        /** Currently Bound Callbacks. */
        private m_bound: Partial<Record<keyof T, Record<number, Functor>>> = {};

        /********************
         *  PUBLIC METHODS  *
         ********************/

        /**
         * Coordinates listening to a channel.
         * @param channel                   Channel to listen to.
         * @param callback                  Listenable callback.
         */
        listen<K extends keyof T>(channel: K, callback: Bindable<T[K]>) {
            const uid = this.m_next(); // get the next UID
            const map = (this.m_bound[channel] ?? {}) as Record<number, Functor>;
            map[uid] = callback; // assign the callback;
            this.m_bound[channel] = map; // set the current map
            return uid; // and return the UID key
        }

        /**
         * Coordinates ignoring callbacks from a given channel. If none are given, removes all.
         * @param channel                   Channel to ignore.
         * @param keys                      Callback keys.
         */
        ignore<K extends keyof T>(channel: K, ...keys: number[]) {
            // if no keys are given, then attempt removing ALL of them
            if (!keys.length) keys = Object.keys(this.m_bound[channel] ?? {}).map((n) => parseInt(n));

            // begin deleting straight away
            const map = this.m_bound[channel];
            if (map) keys.forEach((key) => delete map[key]);

            // return the total items removed
            return keys.length;
        }

        /**
         * Coordinates triggering an event for the given channel.
         * @param channel                   Channel to trigger.
         * @param args                      Callback arguments.
         */
        async trigger<K extends keyof T>(channel: K, ...args: T[K]) {
            const callbacks = Object.values(this.m_bound[channel] ?? {});
            callbacks.forEach((cb) => cb(...args));
        }
    }
}
