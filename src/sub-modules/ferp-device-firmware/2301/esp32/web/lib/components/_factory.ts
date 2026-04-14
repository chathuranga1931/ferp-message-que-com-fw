/// Vendor Modules
import { Cash } from 'cash-dom';

/// AGI-Drive Replacements
import { Listable } from './listable';
import * as Inputs from './inputs';

/** Element Resolution Factory. */
export namespace Factory {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Resolvable Components. */
    export interface IResolvable {
        resolve(el: any): Cash;
    }

    /****************
     *  PROPERTIES  *
     ****************/

    /** Internal resolution cache. */
    const m_cache = new Map<string, IResolvable>();

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Initialises the service. */
    export const init = async () => {
        register('data-list', Listable);
        register('input-item', Inputs.Item);
        register('input-from', Inputs.Generic);
        register('input-radios', Inputs.Radios);
    };

    /**
     * Coordinates registering a custom element resolver.
     * @param tagName                       Tag-name of element.
     * @param handler                       Resolution handler.
     */
    export const register = (tagName: string, handler: IResolvable) => m_cache.set(tagName, handler);

    /** Coordinates all required replacements on a given parent. */
    export const replace = ($parent: Cash) => {
        // iterate over all the available replacements and replace them as needed
        for (const tagName of m_cache.keys()) {
            const $replacable = $parent.find(tagName);
            if (!$replacable.length) continue;

            const { resolve } = m_cache.get(tagName)!;
            const $separate = $replacable.get().map((el) => $(el));
            $separate.forEach(($el) => $el.replaceWith(resolve($el)));
        }
    };
}
