/// Vendor Modules
import { Cash, Selector } from 'cash-dom';

/// AGI-Drive Modules
import { Fetch } from '../../utils/fetch';
import { Factory } from '../../components/_factory';
import { Maybe } from '../../utils/monads/maybe';
import { Alert } from '../../components/alert';
import { Context } from '../../utils/context';
import { Listable } from '../../components/listable';

import * as Inputs from '../../components/inputs';

/** Page Abstraction. */
export abstract class Page<T extends string = string> {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Page Title. */
    abstract readonly title: string;

    /** Gets the base label instance. */
    abstract readonly label: string;

    /** Denotes ignoring lower-case conversion. */
    readonly ignoreLowercase: boolean = false;

    /** Internal Page Content. */
    private m_$content: Cash;

    /** All available input elements. */
    private m_$inputs = $();

    /** Initialisation flag. */
    private m_ready = false;

    /** Denotes if the page is currently active. */
    private m_active = false;

    /** Denotes the current connection state. */
    protected m_state = false;

    /**********************
     *  GETTER / SETTERS  *
     **********************/

    /** Converts a pages label into a usable `href` attribute. */
    get href(): string {
        return `#${this.label}`;
    }

    /** Gets the pages associated tab element. */
    get $tab() {
        return $(`.content-tab-link[href="${this.href}"]`).parent();
    }

    /** Gets all available page inputs. */
    get $inputs() {
        return this.m_$inputs;
    }

    /** Gets the core content parent. */
    get $content() {
        return this.m_$content;
    }

    /** Gets the page's initialisation state. */
    get ready() {
        return this.m_ready;
    }

    /** Page activeness flag. */
    get active() {
        return this.m_active;
    }

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Coordinates preloading the page instance. */
    async preload() {
        // stop if the page is already constructed
        if (this.m_ready) return;

        // generate the required content
        this.m_$content = $('<div class="page-instance">').append(await Fetch.html(this.label));
        Factory.replace(this.m_$content); // ensure we conduct a replacement
        this.m_$inputs = $('input, select', this.m_$content);

        // coordinate custom page preloading
        await this.m_preload();

        // lastly register any events required
        this.registerListeners();

        // and declare as ready for showing
        this.m_ready = true;
    }

    /** Shows the page. */
    async show() {
        // preload the page if necessary
        if (!this.m_ready) await this.preload();

        // set the required tab as active
        this.$tab.addClass('active');

        // call the custom attach handler
        await this.m_onAttach();

        // and set as currently active
        this.m_active = true;
    }

    /** Hides the page. */
    async hide() {
        // call the detach handlers
        await this.m_onDetach();

        // set the required tab as inactive
        this.$tab.removeClass('active');
        this.$content.detach(); // remove naturally

        // and set as inactive
        this.m_active = false;
    }

    /**
     * Wrapped cash query method.
     * @param selector                  Selector to get.
     */
    $(selector: Selector) {
        return $(selector, this.m_$content);
    }

    /**
     * Gets a pages listable parent by key.
     * @param key                       Key value of listable.
     */
    $listable(key: string) {
        return $(`#${this.label}-${key} .data-list`, this.m_$content);
    }

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /** Custum preloading. */
    protected async m_preload() {}

    /** Custom `show` functionality. */
    protected async m_onAttach() {}

    /** Custom `hide` functionality. */
    protected async m_onDetach() {}

    /** Custom event registration. */
    protected m_registerListeners() {}

    /** Base Action Handler. */
    protected m_onAction(action: T, self: Cash): Utils_t.Promisable<void> {}

    /** Disables all available inputs and buttons. */
    protected m_disable(state: boolean) {
        // set the current inputs state
        this.$inputs.prop('disabled', state);

        // also ensure all buttons are disabled too
        this.$content.find('button:not(.card-toggle)').prop('disabled', state);

        // if we are in an ALIVE state, then trigger the `input` event for all active inputs
        if (!state) this.m_trigger();
    }

    /** Triggers all available inputs. */
    protected m_trigger($context?: Cash) {
        ($context ?? this.$inputs).filter('[name="text"], [name="password"], select, :checked').trigger('input');
    }

    /**
     * Coordinates updating generic input values with the given objectified data.
     * @param data                                  Primative value data.
     */
    protected m_refreshInputs<T extends Record<keyof T, string | boolean | number>>(data: T) {
        Object.entries<any>(data).forEach(([key, value]) => {
            const $input = this.$inputs.filter(`[name=${key}]`);
            const type = $input.attr('type') as Inputs.Generic.Type | 'checkbox';

            if (type === 'checkbox') $input.prop('checked', value as boolean);
            else if (type === 'radio') $input.filter(`[value="${value}"]`).prop('checked', true);
            else $input.val(value);
        });
    }

    /**
     * Resolves all available inputs as needed.
     * @param $context                              Inputs to resolve.
     */
    protected m_resolveInputs<T extends object>($context: Cash): Maybe.IMaybe<T> {
        // if we have any invalid inputs, return a bad value
        if ($context.hasClass('is-invalid')) return Maybe.None();

        // resolve the suitable cash-elements
        const $inputs = $context
            .get()
            .map((el) => $(el))
            .filter(($el) => $el.attr('type') !== 'radio' || $el.is(':checked'));

        // resolve the available { name, value } entries
        const entries = $inputs.map(($el) => {
            const name = $el.attr('name')!; // resolve the base name
            const type = $el.attr('type') as Inputs.Generic.Type | 'checkbox';

            if (type === 'checkbox') return { name, value: $el.is(':checked') };
            else if (['radio', 'number'].includes(type)) return { name, value: parseFloat($el.val() as string) };
            return { name, value: $el.val() };
        });

        // and return the resulting data as an object
        return Maybe.Some(entries.reduce((obj, { name, value }) => ({ ...obj, [name]: value }), {} as any));
    }

    /**
     * Coordinates saving a given set of data to a prescribed endpoint.
     * @param endpoint                                  Endpoint to save to.
     * @param $context                                  Context of inputs.
     * @param modifier                                  Optional output modifier.
     */
    protected async m_saveInputs<T extends object, U extends object = never>(
        endpoint: Fetch.Endpoint,
        $context: Cash,
        modifier?: (data: T) => U
    ) {
        // force-trigger all inputs
        this.m_trigger($context);

        // ensure we have validated the data correctly
        const data = this.m_resolveInputs<T>($context);

        // alert if not inputs are valid
        if (data.is('none'))
            return Alert.create({
                context: Context.DANGER,
                title: 'Some Inputs are Invalid',
                $content: 'To continue with the current action, all inputs need to be filled out properly',
            });

        // since we have some valid data, then attempt saving to the desired endpoint
        return this.m_saveData(endpoint, (modifier ? data.map(modifier) : data).unwrap());
    }

    /**
     * Coordinates manually saving data when given.
     * @param endpoint                          Endpoint to save to.
     * @param data                              Data to attempt saving.
     */
    protected async m_saveData<T extends object>(endpoint: Fetch.Endpoint, data: T) {
        const result = await Fetch.request(endpoint, data as any);
        const error = result.is('error') ? result.value() : undefined;

        // finalise by showing a save alert
        Alert.create({
            $content: error ?? [],
            context: Context[error ? 'DANGER' : 'SUCCESS'],
            title: error ? `Failed to Save ${this.title}` : `Saved ${this.title}`,
        });
    }

    /********************
     *  EVENT HANDLERS  *
     ********************/

    /** Coordinates registering global/custom page events. */
    registerListeners() {
        // stop if already initialised
        if (this.ready) return;

        // allow handling of page actions
        $('[data-action]', this.m_$content).on('click', (event: Event) =>
            this.m_delegateActions($(event.target as HTMLElement))
        );

        // setup section visibility
        $('.card-toggle', this.m_$content).on('click', this.m_onVisibilityToggle);

        // fix up the input handling
        this.$inputs.on('invalid', (event) => event.preventDefault());

        // once complete, also setup custom events
        this.m_registerListeners();
    }

    /** Coordinates action delegation. */
    private m_delegateActions($el: Cash) {
        const value = $el.attr('data-action'); // gets the base value
        if (value !== undefined) this.m_onAction(value as T, $el);
    }

    /** Coordinates visibility toggling for `.card-toggle` elements. */
    private m_onVisibilityToggle(this: HTMLElement) {
        const $btn = $(this); // get the base cash reference
        const $parent = $(this).parent(); // and parent reference
        $parent.toggleClass('inactive'); // set the current activeness

        // update the required display elements
        $btn.children().toggleClass('fa-angles-down fa-angles-left');
    }
}
