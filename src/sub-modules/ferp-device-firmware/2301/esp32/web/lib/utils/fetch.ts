/// Vendor Modules
import { Cash } from 'cash-dom';
import { Status_t } from '../services/status';

/// AGI-Drive Modules
import { Result } from './monads/result';

/// AGI-Drive Endpoint Typings

/** Fetch Wrappers */
export namespace Fetch {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Available Fetch Methods. */
    export type Method = 'GET' | 'POST' | 'PUT' | 'DELETE';

    /** Base Result Interface. */
    export type IResult<K extends string> = { result: K };

    /** Successful Result Interface. */
    export type ISuccess = IResult<'success'>;

    /** Fetch Endpoint Interface. */
    export interface IEndpoint<R, T extends object = never> {
        method?: Method; // method to force
        kind?: string; // endpoint requestor value
        __body__?: T; // transient data export
        __retval__?: R; // transient return value
    }

    /** Endpoint Keys. */
    export type Endpoint = keyof typeof ENDPOINTS;

    /****************
     *  PROPERTIES  *
     ****************/

    /** Timeout Error Message. */
    const m_TIMEOUT_ERROR = 'The current request timed out. Could not establish a connection with the server.';

    /** Available AJAX Endpoints. */
    export const ENDPOINTS = {
        'user.data': m_ep<Status_t.IData>({ kind: 'data' }),
        'user.save': m_ep<ISuccess>({ kind: 'save' }),
        'user.stamode': m_ep<ISuccess>({ kind: 'stamode' }),
    } as const;

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Coordinates a simple HTML getter request.
     * @param url                           URL for HTML.
     */
    export const html = async (url: string): Promise<Cash> => {
        return fetch(`/html/${url}.html`)
            .then((res) => res.text())
            .then((text) => $(text))
            .catch(() => $());
    };

    /**
     * Safe request handler for fetching data.
     * @param key                               Request key.
     * @param body                              Body to send.
     */
    export const request = async <K extends Endpoint>(
        key: K,
        body?: NonNullable<ReturnType<typeof ENDPOINTS[K]>['__body__']>
    ): Result.IPromise<NonNullable<ReturnType<typeof ENDPOINTS[K]>['__retval__']>, string> => {
        // format the required fetch request
        const req = m_format(key, body);
        const url = `/ajax/${key.split('.')[0].toLowerCase()}`;

        // prepare an abort controller
        const abortable = m_abortable();
        const { signal, timeout, disposed } = abortable;

        // finally make the required fetch request
        return fetch(url, { ...req, signal })
            .then((res) => res.json())
            .then((value) => Result.Okay(value))
            .catch((err: any) => Result.Error(disposed ? m_TIMEOUT_ERROR : err.message))
            .finally(() => clearTimeout(timeout)) as any;
    };

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /**
     * Formats a request for the given endpoint.
     * @param key                                   Endpoint key.
     * @param body                                  Body required.
     */
    const m_format = <K extends Endpoint>(key: K, body?: any) => {
        // format the endpoint details as needed
        const opts: ReturnType<typeof ENDPOINTS[K]> = (<any>ENDPOINTS[key])(body);

        // construct the base request instance
        const request: RequestInit = {
            method: opts.method ?? 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        };

        // prepare an initial body
        if (opts.kind) {
            if (opts.__body__ === undefined) (<any>opts.__body__) = {};
            (<any>opts.__body__)['request'] = opts.kind;
        }

        // if given a body, then append
        if (opts.__body__ !== undefined) request.body = new URLSearchParams(opts.__body__ as any).toString();

        // return the resulting request
        return request;
    };

    /** Generate an abortable instance. */
    const m_abortable = () => {
        let disposed = false;
        const controller = new AbortController();
        const timeout = setTimeout(() => ((disposed = true), controller.abort()), 1000);
        return { signal: controller.signal, disposed, timeout };
    };

    /**
     * Endpoint constructor.
     * @param opts                              Endpoint options.
     */
    function m_ep<R, T extends object = never>(opts: IEndpoint<R, T> = {}) {
        return (...args: never extends T ? [] : [T]): IEndpoint<R, T> => ({ ...opts, __body__: args[0] });
    }
}
