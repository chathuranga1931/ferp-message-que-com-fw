/// Vendor Modules
import { Cash } from 'cash-dom';

/// AGI-Drive Modules
import { Maybe } from '../../utils/monads/maybe';
import { throttle } from '../../utils/throttle';

/** Input Validation. */
export namespace Validation {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Invalid Typing. */
    export type Invalid = {
        type: 'error' | 'warning';
        message?: string;
    };

    /** Base Validator Callback. */
    export type Callback = (this: HTMLInputElement) => Maybe.IMaybe<Invalid>;

    /****************
     *  PROPERTIES  *
     ****************/

    /** Pre-defined Validators. */
    const m_PRE_DEFINED: Record<string, Callback> = {
        /** Pattern Validator Factory. */
        pattern: <Callback>function () {
            const RE = new RegExp(this.pattern);
            if (!this.pattern.length || RE.test(this.value)) return Maybe.None();
            return Maybe.Some({
                type: 'error',
                message: this.getAttribute('data-pattern-error') ?? 'Input does not match the required pattern.',
            });
        },

        /** Numeric Validator Factory. */
        numeric: <Callback>function () {
            // need fixed values (expect these NOT to change)
            const min = parseFloat(this.min);
            const max = parseFloat(this.max);
            const value = parseFloat(this.value);

            // integer checking
            const integer = { expect: this.step === '1', is: Number.isInteger(value) };

            // ensure we have a valid value
            if (isNaN(value)) return Maybe.Some({ type: 'error', message: 'Invalid numeric input.' });

            // check all the base items
            if (!isNaN(min) && min > value) return Maybe.Some({ type: 'error', message: 'Number exceeds minimum.' });
            if (!isNaN(max) && max < value) return Maybe.Some({ type: 'error', message: 'Number exceeds maximum.' });
            if (integer.expect && !integer.is) return Maybe.Some({ type: 'error', message: 'Expected an integer.' });

            // finally do a regular pattern match
            return m_PRE_DEFINED['pattern'].call(this);
        },
    };

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Auto-registers a validation callback for a given HTML input.
     * @param $input                                    Parent input.
     */
    export const register = ($input: Cash) => {
        // ensure numeric types are handled properly
        if ($input.prop('type') === 'number') custom($input, m_PRE_DEFINED['numeric']);
        else if ($input.prop('pattern').length) custom($input, m_PRE_DEFINED['pattern']);
    };

    /**
     * Adds a custom validation callback to a given input.
     * @param $input                                    Parent input.
     * @param callback                                  Callback to use.
     */
    export const custom = ($input: Cash, callback: Callback) => {
        // construct a simple display wrapper for input validation
        const wrapper = throttle(50, () => {
            const input = $input.get(0);
            const result = input
                ? callback.call(input)
                : (Maybe.Some({ type: 'warning', message: 'Invalid input element.' }) as ReturnType<Callback>);

            // make a reference to validity
            const invalid = result.is('some');

            // add the tooltip element if required
            if ($input.next().attr('data-tooltip') === undefined) {
                $input.after('<span data-tooltip="">').parent().addClass('position-relative');
            }

            // and update the tooltip accordingly
            $input.next().attr('data-tooltip', invalid ? result.unwrap().message ?? 'Invalid input.' : '');

            // update the current styling
            $input.toggleClass('is-invalid', invalid);
        });

        // and ensure the validation occurs
        $input.on('input', wrapper);
    };
}
