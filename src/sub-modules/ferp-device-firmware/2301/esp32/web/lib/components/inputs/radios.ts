/// Vendor Modules
import { Cash } from 'cash-dom';
import { Generic } from './generic';
import { Enum } from '../../utils/enum';

/** Radios Resolve. */
export namespace Radios {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Available Radio Enumerations (for resolution). */
    const m_ENUMS = {};

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Coordinates resolving a given `fake` HTML element for the required "RADIOS" list.
     * @param $el                           Element to resolve.
     */
    export const resolve = ($el: Cash): Cash => {
        const name = $el.attr('name') as keyof typeof m_ENUMS; // get the base name
        const _enum_impl = m_ENUMS[name]; // get the associated enumeration

        // and begin mapping to suitable toggles to append
        const $toggles = Enum.map(_enum_impl, (key, value: string) =>
            Generic.toggle('radio', { name, value, class: 'mr-md-10', label: key })
        );

        // finall wrap all the radio inputs into one form item
        return $('<div class="form-group justify-content-end">')
            .addClass($el.prop('class'))
            .append(...$toggles);
    };
}
