/// Vendor Modules
import { Cash } from 'cash-dom';

/** Coordinates Password Visibility Functionality. */
export namespace Password {
    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Coordinates attaching the given input names with password visibility toggles.
     * @param names                                     Names of inputs.
     */
    export const attach = ($ctx: Cash, ...names: string[]) =>
        names.forEach((name) => $(`input[name="${name}"]`, $ctx).next().find('button').on('click', m_onClick));

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /** Updates an inputs visibility when invoked. */
    const m_onClick = function (this: HTMLButtonElement) {
        const $btn = $(this);
        const $icon = $btn.children().first();

        // check the current visibility
        const visible = $icon.hasClass('fa-eye');

        // update the button icon
        $icon.removeClass('fa-eye fa-eye-slash').addClass(`fa-eye${visible ? '-slash' : ''}`);

        // toggle the base inputs type
        $btn.parent()
            .prevAll('input')
            .attr('type', visible ? 'password' : 'text');
    };
}
