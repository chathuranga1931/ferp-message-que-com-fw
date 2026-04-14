/** DOM Typings. */
declare namespace DOM_t {
    /** Class-name extensible properties. */
    export interface IClass {
        class?: string;
    }

    /** Base Extensible Properties. */
    export interface IProperties extends IClass {
        id?: string;
        attrs?: Record<string, any>;
    }

    /** Enforced text properties. */
    export interface IText extends IClass {
        text: string;
    }
}
