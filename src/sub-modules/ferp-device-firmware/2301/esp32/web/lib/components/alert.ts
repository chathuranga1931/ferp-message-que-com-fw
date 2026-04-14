/// Vendor Modules
import { Cash } from 'cash-dom';

/// AGI-Drive Modules
import { Icon } from './icon';
import { Tuple } from '../utils/tuple';
import { Context } from '../utils/context';

/** Alert Functionality. */
export namespace Alert {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Alert Options. */
    export interface IOptions {
        readonly title: string;
        readonly duration?: number;
        context?: Context;
        $content?: Utils_t.Tuple.Maybe<Cash | string>;
    }

    /*************
     *  OBJECTS  *
     *************/

    /** Notification Instance. */
    class Instance implements IOptions {
        /****************
         *  PROPERTIES  *
         ****************/

        readonly title: string; // Base Notification Title.
        readonly duration: number = 5000; // Defaulted display duration.
        context?: Context; // Notification Context.
        $content: Utils_t.Tuple.Maybe<Cash | string> = [];

        /** Base Notification Element. */
        readonly $element: Cash;

        /** Denotes if currently dismissed. */
        private m_dismissed = false;

        /** Internal display timeout. */
        private m_timeout: ReturnType<typeof setTimeout>;

        /******************
         *  CONSTRUCTORS  *
         ******************/

        /**
         * Constructs a new notification instance.
         * @param options                   Alert options.
         */
        constructor(options: IOptions) {
            Object.assign(this, options); // set the current options
            this.$element = this.m_build(); // build the base element instance
            this.$element.on('click', this.m_onClick.bind(this));
            this.m_timeout = setTimeout(() => this.dismiss(), this.duration);
        }

        /********************
         *  PUBLIC METHODS  *
         ********************/

        /** Dismisses the notification. */
        dismiss() {
            // stop if already dismissed
            if (this.m_dismissed) return;

            // clear the base timeout
            clearTimeout(this.m_timeout);

            // make inivisble
            this.$element.addClass('dismissing');
            setTimeout(() => this.$element.remove(), 250);

            // and declare as dismissed
            this.m_dismissed = true;
        }

        /*********************
         *  PRIVATE METHODS  *
         *********************/

        /** Builds the base element instance. */
        private m_build() {
            return $('<div>')
                .addClass(`alert mt-10 ${this.context ? `alert-${this.context}` : ''} `)
                .append(
                    $('<button class="close">').append(Icon.from('times')),
                    $('<h4 class="alert-heading">').text(this.title),
                    ...Tuple.arrayify(this.$content).map(($item) =>
                        typeof $item === 'string' ? $('<span>').text($item) : $item
                    )
                );
        }

        /********************
         *  EVENT HANDLERS  *
         ********************/

        /**
         * Handles on-click events.
         * @param event                     Base Event.
         */
        private m_onClick(event: Event) {
            const $close = $(event.target as HTMLElement).closest('.close');
            if ($close.length) this.dismiss();
        }
    }

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Creates a new instance of a notification.
     * @param options                       Constructor Options.
     */
    export const create = (options: IOptions) => {
        const instance = new Instance(options); // build the instance
        $('#alerts-wrapper').append(instance.$element); // append to the notification display
    };
}
