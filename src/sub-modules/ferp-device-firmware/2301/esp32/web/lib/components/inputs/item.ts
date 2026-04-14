/// Vendor Modules
import { Cash } from 'cash-dom';

/** Input Item Resolver. */
export namespace Item {
    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Coordinates resolving a given `fake` HTML element to the required result.
     * @param $el                                   Element to resolve.
     */
    export const resolve = ($el: Cash): Cash => {
        // descructure the given options
        const title: string = $el.data('title');
        const desc: string | undefined = $el.data('description');

        // construct the base content
        const item = $('<div class="input-list-item">').addClass($el.attr('class') ?? '');
        const body = $('<div class="input-item-body">').addClass($el.attr('data-body-class') ?? '');
        const header = $('<div class="input-item-header">').append($('<h5 class="input-item-title">').text(title));
        if (desc) header.append($('<p class="input-item-description">').text(desc));

        // update the internal elements
        item.append(header, body.append($el.children()));

        // return the resulting item
        return item;
    };
}
