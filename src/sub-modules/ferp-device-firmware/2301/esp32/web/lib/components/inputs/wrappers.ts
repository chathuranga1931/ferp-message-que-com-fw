/// AGI-Drive Modules
import { Cash } from 'cash-dom';

/** Input Wrappers. */
export namespace Wrap {
    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Simple `form-group` wrapping.
     * @param $input                            Input to wrap.
     */
    export const group = ($input: Cash) => $('<div class="form-group w-250">').append($input);

    /**
     * Wraps an input with a given hint.
     * @param $input                            Input to wrap.
     * @param message                           Hint message.
     */
    export const hint = ($input: Cash, message: string) =>
        group($input).append($('<div class="form-text text-right">').text(message));
}
