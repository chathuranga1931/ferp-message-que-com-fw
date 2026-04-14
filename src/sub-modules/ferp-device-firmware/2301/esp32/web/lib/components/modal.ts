/// Vendor Modules
import { Cash } from 'cash-dom';

/// AGI-Drive Modules
import { Tuple } from '../utils/tuple';
import { Context } from '../utils/context';

/** Modal Functionality. */
export namespace Modal {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Base Action Interface. */
    export interface IAction extends DOM_t.IText {
        context?: Context;
        handler: Utils_t.Functor.Any<boolean | void>;
    }

    /** Modal Options Interface. */
    export interface IOptions {
        title?: string;
        dismissable?: boolean;
        $content?: Utils_t.Tuple.Maybe<Cash | string>;
        actions?: Utils_t.Tuple.Maybe<IAction>;
    }

    /****************
     *  PROPERTIES  *
     ****************/

    /** Base Modal Element. */
    export const $element = $('.modal');

    /** Modal Content Element. */
    export const $content = $('.modal .modal-content');

    /** Denotes if currently open. */
    let m_instanced = false;

    /** Common Actions */
    export namespace Actions {
        /********************
         *  HELPER METHODS  *
         ********************/

        /** Constructs an actions wrapper. */
        export const $wrapper = () => $('<div class="modal-actions">');

        /**
         * Constructs a simple action button for modals.
         * @param action                            Action options.
         */
        export const $button = ({ text, context, handler }: IAction) => {
            const $btn = $('<button class="btn">').text(text).on('click', handler);
            if (context) $btn.addClass(`btn-${context}`);
            return $btn;
        };

        /**
         * Constructs a simple dismisser action.
         * @param action                            Base Action Options.
         */
        export const dismisser = ({ handler, ...rest }: Utils_t.Optional<IAction, 'handler'>): IAction => ({
            ...rest,
            handler: () => (handler?.() ?? true) && dismiss(),
        });

        /****************
         *  PROPERTIES  *
         ****************/

        /** Base Close Action. */
        export const close = dismisser({ text: 'Close' });

        /** Base Cancel Action. */
        export const cancel = dismisser({ text: 'Cancel', context: Context.PRIMARY });
    }

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Initialises dismiss handling. */
    export const init = async () => {
        $element.on('click', m_onClick);
    };

    /**
     * Coordinates a simple Modal request. If a modal is already shown, then
     * the request fails to be coordinated.
     * @param options                           Modal Options.
     */
    export const request = (options: IOptions) => {
        if (m_instanced) return; // do nothing if already shown

        // destructure some items
        const { title, actions, $content: $items, dismissable } = options;

        // construct the required content values
        const $body = Tuple.arrayify($items)?.map(($item) =>
            typeof $item === 'string' ? $('<span>').text($item) : $item
        );

        // append the title and actions wrapper as necessary
        if (title) $body.unshift($('<h5 class="modal-title text-capitalize">').html(title));
        const $wrapper = Actions.$wrapper(); // generate the actions wrapper

        // prepare the required actions (if none then append the close button)
        $wrapper.append(...Tuple.arrayify(actions, Actions.close).map(Actions.$button));

        // actually append the required content
        $content.append(...$body, $wrapper);

        // denote if currently dismissable
        if (dismissable) $element.prop('data-overlay-dismissal-disabled', '');
        else $element.removeProp('data-overlay-dismissal-disabled');

        // declare as instanced and show the modal
        m_instanced = true;
        halfmoon.toggleModal('modal-instance');
    };

    /** Manually closes any currently open modal instance. */
    export const dismiss = () => {
        halfmoon.toggleModal('modal-instance');
        m_instanced = false; // declare as hidden
        $content.children().remove(); // remove all content
        return true;
    };

    /********************
     *  EVENT HANDLERS  *
     ********************/

    /**
     * Coordinates generic modal click events.
     * @param event                             Event to coordinate.
     */
    const m_onClick = function (this: HTMLElement, event: Event) {
        const $target = $(<HTMLElement>event.target); // get the base target
        const dismissable =
            $target.closest('[data-dismiss="modal"]').length ||
            $target.hasClass('modal') ||
            $target.hasClass('modal-dialog');

        // handle any and all dismissal types
        if (dismissable) {
            event.preventDefault();
            event.stopPropagation();
            return dismiss(); // handle manual dismissal
        }
    };
}
