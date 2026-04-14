/// AGI-Drive Modules
import { Maybe } from './maybe';

/** Result Monads. */
export namespace Result {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Available Result Outcomes. */
    export type Outcome = 'okay' | 'error';

    /** Result Mapping. */
    export interface IMap<T, E> {
        okay: IOkay<T, E>;
        error: IError<T, E>;
    }

    /** Result Predicate Typing. */
    export type Predicate = () => boolean;

    /** Pattern Matcher Handlers. */
    export interface IPattern<T, E, U> {
        readonly okay: (value: T) => U;
        readonly error: (value: E) => U;
    }

    /** Result Typing. */
    export interface IResult<T, E> {
        kind: Outcome;
        is<O extends Outcome>(kind: O): this is IMap<T, E>[O];
        value(): T | E;
        unwrap(opt?: T): T;
        match<U>(fn: IPattern<T, E, U>): U;
        maybe(): Maybe.IMaybe<T>;
        map<U>(fn: (value: T) => U): IResult<U, E>;
        flatMap<U>(fn: (value: T) => IResult<U, E>): IResult<U, E>;
    }

    /** Promisified Result Typing. */
    export type IPromise<T, E> = Promise<IResult<T, E>>;

    /** Okay Result Typing. */
    export interface IOkay<T, E = never> extends IResult<T, E> {
        kind: 'okay';
        value(): T;
        unwrap(): T;
        map<U>(fn: (value: T) => U): IOkay<U, E>;
        match<U>(fn: IPattern<T, never, U>): U;
    }

    /** @ts-ignore : Error Result Typing. */
    export interface IError<T, E> extends IResult<T, E> {
        kind: 'error';
        value(): E;
        unwrap(opt?: T): undefined extends T ? never : T;
        map<U>(fn: (value: T) => U): IError<U, E>;
        flatMap<U>(fn: (value: T) => IResult<U, E>): IResult<never, E>;
    }

    /********************
     *  PUBLIC METHODS  *
     ********************/

    export const predicate = <T, E>(pred: Predicate, value: T, error: E): IResult<T, E> =>
        pred() ? Okay(value) : Error(error);

    /***************
     *  FACTORIES  *
     ***************/

    /** Constructs a void result. */
    export const Void = <E = never>(): IOkay<void, E> => Okay<void, E>(void 0);

    /**
     * Constructs an okay result.
     * @param value                   Success value.
     */
    export const Okay = <T, E = never>(value: T): IOkay<T, E> => ({
        kind: 'okay',
        is: (kind) => kind === 'okay',
        maybe: () => Maybe.Some(value),
        value: () => value,
        unwrap: () => value,
        match: (fn) => fn.okay(value),
        map: (fn) => Okay(fn(value)),
        flatMap: (fn) => fn(value),
    });

    /**
     * Constructs an error result.
     * @param error                   Error value.
     */
    export const Error = <T, E>(error: E): IError<T, E> => ({
        kind: 'error',
        is: (kind) => kind === 'error',
        maybe: () => Maybe.None(),
        match: (fn) => fn.error(error),
        map: () => Error(error),
        flatMap: () => Error(error),
        value: () => error,
        unwrap: <any>((opt?: T) => {
            if (opt === undefined) throw error;
            return opt;
        }),
    });
}
