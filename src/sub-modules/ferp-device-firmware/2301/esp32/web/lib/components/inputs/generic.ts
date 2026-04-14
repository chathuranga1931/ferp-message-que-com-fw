/// Vendor Modules
import { Cash } from 'cash-dom';
import { UUID } from '../../utils/uuid';

/// AGI-Drive Modules
import { Validation } from './validation';

/** Generic Input Constructors. */
export namespace Generic {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Available Toggle Types. */
    export type Toggle = 'switch' | 'radio';

    /** Available Input Types. */
    export type Type = 'text' | 'number' | 'password' | Toggle;

    /** Base Input Options. */
    export interface IOptions<V = any, T extends Type = Type> extends DOM_t.IProperties {
        type: T;
        value?: V;
        name?: string;
        pattern?: string | RegExp;
        readonly?: boolean;
        placeholder?: string;
        validator?: Validation.Callback;
    }

    /** Base Toggle Options. */
    export interface IToggleOptions<T extends Toggle>
        extends Omit<
            IOptions<T extends 'switch' ? boolean : string, T>,
            'type' | 'pattern' | 'placeholder' | 'validator'
        > {
        label?: string;
    }

    /***************
     *  FACTORIES  *
     ***************/

    /**
     * Base input generator.
     * @param options                   Given input options.
     */
    export const base = (options: IOptions) => {
        // generate the base element with required typing
        const $input = $('<input class="form-control">').prop('type', options.type);

        // destructure and begin setting the default properties
        const { value, validator, pattern, readonly, attrs, class: classes, ...rest } = options;

        // can easily set most of the properties
        $input
            .addClass(classes ?? '')
            .attr('spellcheck', 'false')
            .attr(rest as any);

        // and finish with some special values
        if (value !== undefined) $input.val(value);
        if (readonly) $input.prop('readonly', 'true');
        if (attrs !== undefined) $input.attr(attrs);
        if (pattern !== undefined) {
            const usingRE = typeof pattern !== 'string';
            const source = usingRE ? pattern.source.replace(/[\\]/g, '\\$&') : pattern;
            $input.attr('pattern', source);
        }

        // append a validator if necessary
        validator ? Validation.custom($input, validator) : Validation.register($input);

        // return the resulting input
        return $input;
    };

    /**
     * Constructs a text input with the given properties.
     * @param options                           Input options.
     */
    export const text = (options: Partial<IOptions<string, 'text' | 'password'>> = {}) =>
        base({
            type: 'text',
            value: 'Unknown',
            pattern: /(?!^$)([^\s])/,
            attrs: { 'data-pattern-error': 'Input cannot be empty.' },
            class: options.readonly ? 'text-right pointer-events-none' : '',
            ...options,
        });

    /**
     * Constructs a numeric input with the given properties.
     * @param options                           Input options.
     */
    export const numeric = (options: Omit<IOptions<number>, 'type'> = {}) =>
        base({ type: 'number', class: options.readonly ? 'text-right pointer-events-none' : '', ...options });

    /**
     * Constructs a togglable element.
     * @param type                              Type of toggle.
     * @param options                           Toggle options.
     */
    export const toggle = <T extends Toggle>(type: T, options: IToggleOptions<T> = {}) => {
        const { id, class: classes, attrs, label, value, ...rest } = options; // destructure the options
        const $toggle = $(`<div class="custom-${type}">`).addClass(classes ?? '');

        // construct the base input we need
        const $input = $(`<input type="${type === 'radio' ? 'radio' : 'checkbox'}">`)
            .attr('id', id ?? UUID.V4())
            .attr(rest as Record<string, string>);

        // attach the value based on the type
        $input.prop(typeof value === 'boolean' ? 'checked' : 'value', value);

        // attach a toggle effect if needed
        if (type === 'switch') {
            $input.on('input', function (this: HTMLInputElement) {
                this.nextElementSibling!.innerHTML = this.checked ? 'Enabled' : 'Disabled';
            });
        }

        // construct the required label as well
        const $label = $('<label>').attr('for', $input.attr('id')!);
        if (label) $label.text(label);
        else if (type === 'switch') $label.text(value ? 'Enabled' : 'Disabled');

        // actually append the input and label
        return $toggle.append($input, $label);
    };

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Constructs an input from a given underlying input.
     * @param $with                             Base input.
     */
    export const resolve = ($with: Cash) => {
        // construct an element that copies the original properties
        const $input = m_fromType(($with.attr('type') ?? 'text') as Type);
        const className = $input.prop('class');

        // iterate over the properties to copy (ensuring we have the actual input)
        Array.from<Node>($with.prop('attributes'))
            .filter((node) => node.nodeName !== 'type')
            .forEach((node) =>
                ($input.is('input, select') ? $input : $input.find('input, select')).attr(
                    node.nodeName,
                    node.nodeValue!
                )
            );

        // update some special values
        $input.addClass(className);
        $input.prop('value', $with.attr('value') ?? 'Unknown');
        if ($input.is('[readonly]')) $input.addClass('text-right pointer-events-none');

        // return the resulting element
        return $input;
    };

    /**
     * Gets the associated value of an input.
     * @param $input                            Input to get value of.
     */
    export const value = ($input: Cash) => {
        const type = $input.prop('type');
        const tagName = $input.prop('tagName');

        if (type === 'checkbox') return $input.is(':checked');
        else if (tagName === 'SELECT') return parseInt($input.val() as string);
        else if (type === 'number') return parseFloat($input.val() as string);
        return $input.val();
    };

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /**
     * Constructs a generic input from a given type.
     * @param type                              Type to stem from.
     */
    const m_fromType = (type: Type) => {
        if (type === 'switch' || type === 'radio') return toggle(type);
        return type === 'number' ? numeric() : text({ type });
    };
}
