/// Vendor Modules
import { Cash, Selector } from 'cash-dom';

/// AGI-Drive Modules
import { Icon } from '../icon';
import { Modal } from '../modal';
import { Enum } from '../../utils/enum';
import { Context } from '../../utils/context';

import * as Inputs from '../inputs';
import { Alert } from '../alert';

/** Handles constructing listable data displays. */
export namespace Listable {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Types of available inputs. */
    export type Input = 'text' | 'boolean' | 'enum' | 'number' | 'custom';

    /** Types of listable actions. */
    export type Action = 'create' | 'edit' | 'remove' | 'update';

    /** Optional property modifiers. */
    export interface IModifiers {
        enum?: Enum.Generic<any>;
        hint?: string;
        min?: number;
        max?: number;
        step?: number;
        pattern?: number;
        onInput?: (this: HTMLInputElement) => void;
        custom?: () => Cash;
    }

    /** Base Listable Options. */
    export interface IDetails<T extends any[][]> {
        __data__?: T;
        title: string;
        inputs: Input[];
        descriptions: string[];
        headers: string[];
        identifier: number;
        presets: any[];
        modifiers: Array<IModifiers | undefined>;
        onUpdate?: ($context: Cash, index: number) => void;
        onDelete?: (index: number) => void;
    }

    /****************
     *  PROPERTIES  *
     ****************/

    /** Internal context manipulation. */
    const m_contexts = {
        create: 'primary',
        edit: 'primary',
        remove: 'danger',
        update: 'success',
    };

    /** Common Modifiers. */
    export const MODS = {
        UINT8: { min: 0, max: 255, step: 1, hint: 'Unsigned 8-bit value' } as IModifiers,
        UINT16: { min: 0, max: 65535, step: 1, hint: 'Unsigned 16-bit value' } as IModifiers,
        DOUBLE: { hint: 'Any valid float value' } as IModifiers,
    };

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Constructs a series of `data-list` elements. */
    export const resolve = (_: Cash): Cash => {
        // generate the base list and actions
        const $list = $('<div class="data-list">');
        const $actions = $('<div class="card-actions">').append(
            action('Create', 'create'),
            $('<button class="btn btn-success ml-10">').attr('data-action', 'listable.update').text('Update')
        );

        // construct a combined element to return
        return $('<div class="card-body">').append($list, $actions);
    };

    /**
     * Constructs a simple listable action button.
     * @param text                              Text to use.
     * @param action                            Action to coordinate.
     */
    export const action = (text: Selector, action: Action) =>
        $('<button>')
            .addClass(`btn btn-${m_contexts[action]}`)
            .attr('data-listable-action', action)
            [typeof text === 'string' ? 'text' : 'append'](text);

    /**
     * Coordinates constructing / refreshing the given listable items.
     * @param $parent                               Listable container.
     * @param data                                  Data to refresh.
     * @param details                               Base display details.
     */
    export const refresh = <T extends any[][]>($parent: Cash, data: T, details: IDetails<T>) => {
        // construct all the required items
        const $items = data.map((props) =>
            $('<div class="data-list-item">')
                .attr('data-value', JSON.stringify(props, null, 1))
                .append(
                    ...props
                        .filter((_, ii) => details.identifier === ii)
                        .map((value) => m_group(details.headers[details.identifier], value)),
                    action(Icon.from('expand'), 'edit'),
                    action(Icon.from('times'), 'remove')
                )
        );

        // and append onto the base parent
        $parent.children().remove();
        $parent.append(...$items);
    };

    /**
     * Converts all given `data-list-item` elements into a single shadowed input.
     * @param $context                      Context of data-list.
     */
    export const collect = ($context: Cash) => {
        // need a name to save from
        const name = $context.attr('id')!.split('-').slice(1).join('-');

        // collect the base values and parse them initially
        const items = $context
            .find('.data-list-item')
            .get()
            .map((el) => JSON.parse(el.getAttribute('data-value') ?? '[]'));

        // output a shadowed input element to encompass the result
        return $('<input type="text">').attr('name', name).val(JSON.stringify(items));
    };

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /**
     * Helper group constructor for the base identifier input.
     * @param header                        Header to use.
     * @param value                         Value to wrap.
     */
    const m_group = (header: string, value: string) =>
        $('<div class="input-group">').append(
            $(`<div class="input-group-prepend"><span class="input-group-text">${header}</span></div>`),
            Inputs.Generic.text({ value, readonly: true, class: 'pointer-events-none' })
        );

    /**
     * Helper method to construct listable modal titles.
     * @param title                         Base title string.
     * @param action                        Action being coordinated.
     */
    const m_title = (title: string, action: string) =>
        `<span class="badge font-size-20 text-monospace">${title}</span> &ndash; ${action}`;

    /**
     * Coordinates constructing an input for a listable element with the given option.
     * @param type                          Input typing.
     * @param value                         Optional value.
     * @param modifiers                     Potential modifiers.
     */
    const m_buildInput = (type: Input, value: any, modifiers?: Omit<IModifiers, 'hint' | 'onInput'>): Cash => {
        // base modifiers
        const { enum: _enum_impl, custom, ...mods } = (modifiers ?? {}) as Record<string, string>;

        // generic inputs are fairly simple to construct
        if (type === 'boolean') return Inputs.Generic.toggle('switch', { value });
        else if (type === 'text') return Inputs.Generic.text({ value }).attr(mods);
        else if (type === 'number') return Inputs.Generic.numeric({ value }).attr(mods);
        else if (type === 'custom') return (<any>custom)() as Cash; // custom constructor

        // convert the given enumeration
        const $options = Enum.map<Cash>(_enum_impl as any, (key, value: string) => $('<option>').val(value).text(key));

        // enumerations however need some special care
        return $('<select class="form-control">')
            .append(...$options)
            .val(value);
    };

    /**
     * Constructs listable inputs for a modal display.
     * @param details                       Base details.
     * @param props                         Optional properties.
     * @param index                         List item index value.
     */
    const m_inputs = <T extends any[]>(details: IDetails<T[]>, props?: T, index?: number) => {
        // generate all the required listable data items
        const $items = details.inputs.map((type, ii) => {
            // generate the base `input-item`
            const $item = $('<input-item>').attr({
                'data-description': details.descriptions[ii],
                'data-title': details.headers[ii],
            }) as Cash;

            // append the required input (once built)
            const { hint, onInput, ...mods } = details.modifiers[ii] ?? {};
            let $input = m_buildInput(type, props?.[ii] ?? details.presets[ii], mods);

            // if given a hint then wrap the base input
            if (type !== 'boolean') $input = $('<div class="form-group w-250">').append($input);
            if (hint) $input.append($('<div class="form-text text-right">').text(hint));

            // attach the change handler if set
            if (onInput) $input.find('input, select').on('input', onInput);

            // and finally append to the base item for resolution
            $item.append($input);
            if (type === 'boolean') $item.addClass('no-collapse');

            // finally return the resolved item
            return Inputs.Item.resolve($item).attr('data-name', details.headers[ii]);
        });

        // wrap all the elements into a single form
        const $form = Inputs.Form.wrap(...$items).attr('data-list-index', index?.toString() ?? '-1');

        // finally ensure all the inputs have been validated
        $items.forEach(($item) => $item.find('input, select')?.trigger('input'));

        // return the resulting form
        return $form;
    };

    /********************
     *  EVENT HANDLERS  *
     ********************/

    /**
     * Registers the given context for listable actions.
     * @param $context
     */
    export const register = ($context: Cash, details: Record<string, IDetails<any[][]>>) =>
        $context.off('click').on('click', m_delegateActions(details));

    /**
     * Coordinates a listable action event.
     * @param details                       Listable details available..
     */
    const m_delegateActions = (details: Record<string, IDetails<any[][]>>) => (event: Event) => {
        const $target = $(event.target as HTMLElement).closest('[data-listable-action]');
        const action = $target.attr('data-listable-action') as Action | undefined;
        if (!$target.length || action === undefined) return;

        // get the currently desired context to coordinate the action from
        const context = $target.closest('.card').attr('id')!.split('-').slice(1).join('-');
        const $parent = $target.closest('.data-list-item');

        // delegate out the action required
        if (action === 'create') m_onCreate(context, details[context]);
        else if (action === 'edit') m_onEdit(context, details[context], $parent);
        else if (action === 'remove') m_onRemove(context, details[context], $parent);
    };

    /**
     * Constructs a `create` modal for listable data.
     * @param context                       Base context.
     * @param details                       Listable details.
     */
    const m_onCreate = (context: string, details: IDetails<any[][]>) =>
        Modal.request({
            title: m_title(details.title, 'Create'),
            $content: m_inputs(details),
            actions: [
                Modal.Actions.cancel,
                Modal.Actions.dismisser({
                    text: 'Create',
                    context: Context.SUCCESS,
                    handler: m_updateHandler(context, details),
                }),
            ],
        });

    /**
     * Constructs a `viewer` modal for listable data.
     * @param context                       Base context.
     * @param details                       Listable details.
     * @param $target                       Target parent.
     */
    const m_onEdit = (context: string, details: IDetails<any[][]>, $target: Cash) =>
        Modal.request({
            title: m_title(details.title, 'Viewer'),
            $content: m_inputs(details, JSON.parse($target.attr('data-value') ?? '[]'), $target.index()),
            actions: [
                Modal.Actions.cancel,
                Modal.Actions.dismisser({
                    text: 'Update',
                    context: Context.SUCCESS,
                    handler: m_updateHandler(context, details, $target),
                }),
            ],
        });

    /**
     * Constructs a `remove` modal for listable data.
     * @param context                       Base context.
     * @param details                       Listable details.
     * @param $target                       Target parent.
     */
    const m_onRemove = (context: string, details: IDetails<any[][]>, $target: Cash) =>
        Modal.request({
            title: m_title(details.title, 'Remove'),
            $content: [
                $('<p class="mb-5">').text('Should the following data be removed?'),
                m_group(
                    details.headers[details.identifier],
                    JSON.parse($target.attr('data-value') ?? '[]')[details.identifier]
                ).addClass('mt-10 mb-20'),
            ],
            actions: [
                Modal.Actions.cancel,
                Modal.Actions.dismisser({
                    text: 'Confirm',
                    context: Context.SUCCESS,
                    handler: () => (details.onDelete?.($target.index()), $target.remove(), void 0),
                }),
            ],
        });

    /**
     * Constructs an update handler for given listable context.
     * @param context                       Base context.
     * @param details                       Context details.
     * @param $target                       Target parent.
     */
    const m_updateHandler = (context: string, details: IDetails<any[][]>, $target?: Cash) => () => {
        // get the base list elements
        const $list = $(`[id*=${context}]`).find('.data-list');

        // get the base data to update from
        const data: any[][] = [...$list.find('.data-list-item')].map((el) =>
            JSON.parse(el.getAttribute('data-value') ?? '[]')
        );

        // get all the current modal inputs
        const $inputs = Modal.$content.find('input, select');

        // if any inputs are invalid, then declare and do not allow dismissal
        if ($inputs.hasClass('is-invalid')) {
            Alert.create({
                title: `Could not Update ${context}`,
                context: Context.WARNING,
                $content: 'Some inputs are invalid.',
            });

            // declare as not dismissabled
            return false;
        }

        // generate the suitable data (ignoring the current value)
        const next = [...$inputs].map((input: HTMLInputElement) => Inputs.Generic.value($(input)));

        // if we have been given a parent, then modify
        const index = $target?.index() ?? data.length;
        data[index] = next;

        // force a visual update as required
        refresh($list, data, details);

        // call the on-update handler now
        details.onUpdate?.($list, index);
    };
}
