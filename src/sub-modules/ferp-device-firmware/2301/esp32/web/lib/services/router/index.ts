/// AGI-Drive Modules
import { Page } from './page';
import { Service } from '../_base';

/// Available Pages
import { _Config_page } from '../../pages/config';

/** Page Routing Service. */
class _Router_impl implements Service.From<'init'> {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Available Pages. */
    readonly pages: Page[] = [_Config_page];

    /** Base Page Container. */
    private readonly m_$container = $('.content-body');

    /** Base Title Element. */
    private readonly m_$title = $('#content-title');

    /***********************
     *  GETTERS / SETTERS  *
     ***********************/

    /** Gets the current page location. */
    get location() {
        const href = location.hash;
        return href.trim() ? href : this.pages[0].href;
    }

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /** Initialises the service. */
    async init() {
        // construct the required tabs
        this.m_prepareNavigation();

        // setup a hash-change handler
        window.addEventListener('hashchange', this.reload.bind(this));
    }

    /** Coordinates a reload instance. */
    async reload() {
        return this.load(this.location.replace(/^#/, ''));
    }

    /**
     * Loads a given page reference.
     * @param ref                       Page reference (`label`).
     */
    async load(ref: string) {
        // attempt finding the page we want to load
        const prev = this.pages.find((p) => p.active);
        const curr = this.pages.find((p) => p.label === ref);

        // if the page does not exist, then do nothing
        if (curr === undefined) return;

        // hide the previous page
        prev?.hide();

        // if the page required to be displayed is the same, then refresh the page only
        if (prev?.title === curr.title) return curr.show();

        // otherwise, set the current page title / active tab
        this.m_$title.text(curr.title);

        // prepare the page required
        await curr.show();

        // and update the required content
        this.m_$container.children().detach();
        this.m_$container.append(curr.$content);
    }

    /*********************
     *  PRIVATE METHODS  *
     *********************/

    /** Coordinates preparing navigation tabs.. */
    private m_prepareNavigation() {
        const nav = $('.content-navigation').get(0)!;

        // and begin appending all the required navigation elements
        this.pages
            .map((page) => `<a class="content-tab-link mb-0" href="${page.href}">${page.title}</a>`)
            .map((anchor) => $(`<span class="content-tab">${anchor}</span>`))
            .forEach((tab) => nav.appendChild(tab.get(0)!));
    }
}

/// Singleton Instance.
export const Router = new _Router_impl();
