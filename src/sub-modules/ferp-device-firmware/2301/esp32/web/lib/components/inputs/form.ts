/// Vendor Modules
import { Cash } from 'cash-dom';

/** Input Form Factories. */
export namespace Form {
    /**
     * Wraps a series of items into a singular form.
     * @param $items                        Items to wrap.
     */
    export const wrap = (...$items: Cash[]) =>
        $('<form class="input-list" onsubmit="event.preventDefault(); return false">').append(...$items);
}
