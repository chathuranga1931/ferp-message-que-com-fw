/// Vendor Modules
import { Cash } from 'cash-dom';

/** Coordinates Toggling/Updating Visibility Conditions. */
export namespace Visibility {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Simple Visibility Comparator. */
    export type Handler = (this: HTMLInputElement) => boolean;

    /****************
     *  PROPERTIES  *
     ****************/

    /** Base Handler for regular inputs. */
    export const DEFAULT_HANDLER: Handler = function () {
        return this.checked;
    };

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Attaches the conditional handler for toggle targets.
     * @param $toggle                           Base toggle element.
     * @param $targets                          Targets to toggle.
     * @param comparator                        Comparator function.
     */
    export const attach = ($toggle: Cash, $targets: Cash, comparator: Handler) =>
        $toggle.on('input', m_bindOnToggle($targets, comparator));

    /**
     * Checkbox visibility toggler.
     * @param $toggle                           Base toggle element.
     * @param $targets                          Targets to toggle.
     */
    export const checkbox = ($toggle: Cash, $targets: Cash) => attach($toggle, $targets, DEFAULT_HANDLER);

    /**
     * Radio visibility toggler.
     * @param $toggle                           Base toggle element.
     * @param $targets                          Targets to toggle.
     * @param values                            Radio values.
     */
    export const radio = ($toggle: Cash, $targets: Cash, ...values: string[]) =>
        attach($toggle, $targets, function () {
            return values.includes(this.value);
        });

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /**
     * Coordinates wrapping a suitable input handler for visibility toggling.
     * @param $targets                          Targets to toggle.
     * @param comparator                        Comparator function.
     */
    const m_bindOnToggle = ($targets: Cash, comparator: Handler) => {
        return function (this: HTMLInputElement) {
            $targets.toggle(comparator.call(this) as boolean);
        };
    };
}
